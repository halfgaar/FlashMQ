#include "forward_declarations.h"
#include "types.h"
#include "client.h"
#include "threadglobals.h"
#include "threaddata.h"
#include "trackedsubscriptionstate.h"
#include "logger.h"


bool TrackedSubscriptionState::addTrackedSubscriptionMutation(TrackedSubscriptionMutation &&mut)
{
    auto locked = trackedSubscriptionMutations.lock();
    bool wakeupRequired = locked->empty();
    locked->emplace_back(std::move(mut));
    return wakeupRequired;
}

void TrackedSubscriptionState::stageInFlightTrackedSubscriptions(std::vector<Subscribe> &&subscribes, uint16_t pack_id)
{
    this->inFlightTrackedSubscriptions = std::make_optional<InFlightTrackedSubscription>();
    this->inFlightTrackedSubscriptions->subscribes = std::move(subscribes);
    this->inFlightTrackedSubscriptions->id = pack_id;
    this->inFlightTrackedSubscriptions->tryCount = 0;
}

void TrackedSubscriptionState::stageInFlightTrackedUnsubscriptions(std::vector<Unsubscribe> &&unsubscribes, uint16_t pack_id)
{
    this->inFlightTrackedUnsubscriptions.emplace();
    this->inFlightTrackedUnsubscriptions->unsubscribes = std::move(unsubscribes);
    this->inFlightTrackedUnsubscriptions->id = pack_id;
    this->inFlightTrackedUnsubscriptions->tryCount = 0;
}

void TrackedSubscriptionState::sendInFlightTrackedSubscriptions(Client *network_client)
{
    if (!network_client)
        return;

    if (!this->inFlightTrackedSubscriptions)
        return;

    MqttPacket sub_pack(network_client->getProtocolVersion(), this->inFlightTrackedSubscriptions->id, 0, this->inFlightTrackedSubscriptions->subscribes);
    network_client->writeMqttPacketAndBlameThisClient(sub_pack);

    this->inFlightTrackedSubscriptions->tryCount++;
}

void TrackedSubscriptionState::sendInFlightTrackedUnsubscriptions(Client *network_client)
{
    if (!network_client)
        return;

    if (!this->inFlightTrackedUnsubscriptions)
        return;

    MqttPacket pack(network_client->getProtocolVersion(), this->inFlightTrackedUnsubscriptions->id, this->inFlightTrackedUnsubscriptions->unsubscribes);
    network_client->writeMqttPacketAndBlameThisClient(pack);

    this->inFlightTrackedUnsubscriptions->tryCount++;
}

void TrackedSubscriptionState::resetIterators()
{
    curPosResending.reset();
    resendCount = 0;
    resendTotal = 0;

    curPosPurging.reset();
    purgeCount = 0;
    purgeTotal = 0;
}

std::unique_ptr<ReentrantMap<std::string, TrackedSubscription>> TrackedSubscriptionState::stealTrackedSubscriptions()
{
    resetIterators();
    auto result = std::move(trackedSubscriptions);
    trackedSubscriptions = std::make_unique<ReentrantMap<std::string, TrackedSubscription>>();
    return result;
}

void TrackedSubscriptionState::replaceTrackedSubscriptions(std::unique_ptr<ReentrantMap<std::string, TrackedSubscription>> &&val)
{
    trackedSubscriptions = std::move(val);
    resetIterators();
}

/**
 * @brief This is at the heart of tracking the expanded lazy subscriptions for one particular BridgeState.
 *
 * The tracked subscriptions are first queued by other clients (in other threads) in a list of mutations. This list is then
 * processed by the thread itself. The reason mainly has to do with another reason: resending the tracked subscription after
 * connection loss. The tracked subscriptions need to be processed in order, including whatever subscriptions come in while
 * the resending is going on.
 *
 * This function can be queued from several different contexts, and then perform the correct action. See the modifier argument.
 */
void TrackedSubscriptionState::processTrackedSubscriptionMutations(
    const std::shared_ptr<BridgeState> &bridgeState, const ProcessTrackedSubscriptionMutationsModifier modifier)
{
    assert(ThreadGlobals::getThreadData()->thread_id == pthread_self());

    if (modifier == ProcessTrackedSubscriptionMutationsModifier::FirstFinishResending && curPosResending != trackedSubscriptions->end())
        return;

    constexpr size_t batch_size = 100;

    std::shared_ptr<Session> session = bridgeState->session.lock();

    if (!session)
        return;

    std::shared_ptr<Client> network_client = session->makeSharedClient();

    if (!network_client || !network_client->getAuthenticated())
        return;

    if (modifier == ProcessTrackedSubscriptionMutationsModifier::StartResending)
    {
        curPosResending = trackedSubscriptions->begin();
        inFlightTrackedSubscriptions.reset();
        resendCount = 0;
        resendTotal = trackedSubscriptions->size();

        curPosPurging.reset();
        inFlightTrackedUnsubscriptions.reset();
        purgeCount = 0;
        purgeCount = 0;
    }

    // First cover the retry case. If a SUBACK is properly processed, this is always false.
    if (inFlightTrackedSubscriptions)
    {
        // That means a stray SUBACK caused the requeue of this function, because a matching SUBACK would have
        // cleared the inFlightTrackedSubscriptions.
        if (modifier != ProcessTrackedSubscriptionMutationsModifier::Retry)
            return;

        if (!hasOutdatedInFlightTrackedSubscriptions())
            return;

        if (inFlightTrackedSubscriptions->tryCount > 3)
        {
            Logger::getInstance()->log(LOG_ERROR)
                    << "Tracked lazy subscription with id " << inFlightTrackedSubscriptions->id << " to server '"
                    << network_client->getClientId() << "' went unacknolwleged after " << inFlightTrackedSubscriptions->tryCount
                    << " times. Disconnecting";

            inFlightTrackedSubscriptions.reset();
            network_client->setDisconnectReason("Missing SUBACKs when sending tracked lazy subscriptions");
            ThreadGlobals::getThreadData()->removeClientQueued(network_client);
            return;
        }

        Logger::getInstance()->log(LOG_WARNING)
                << "Missing SUBACK when sending tracked lazy subscription to server '"
                << network_client->getClientId() << "'. Trying again. Try count: " << inFlightTrackedSubscriptions->tryCount;

        sendInFlightTrackedSubscriptions(network_client.get());
        return;
    }

    if (curPosResending != trackedSubscriptions->end())
    {
        std::vector<Subscribe> subscribes;
        std::optional<uint16_t> pack_id;

        size_t batch_entry = 0;
        while (curPosResending != trackedSubscriptions->end() && batch_entry <= batch_size)
        {
            auto cur = curPosResending.lock();
            ++curPosResending;

            if (!cur)
                continue;

            batch_entry++;
            resendCount++;

            if (!pack_id)
            {
                pack_id = session->getNextPacketIdLocked();

                if (!pack_id)
                    return;
            }

            TrackedSubscription &t = cur->val;
            Subscribe &sub = subscribes.emplace_back(t.pattern, t.qos);
            sub.retainAsPublished = true;

            /*
             * At this point, we are resending our subscriptions because the connection got lost. We don't want
             * to cause stray publishes. If the remote end restarted, 'new subscribe only' should take
             * care of the remote server getting new subscriptions as clients reconnect.
             */
            sub.retainHandling = RetainHandling::SendRetainedMessagesAtNewSubscribeOnly;

            if (Logger::getInstance()->wouldLog(LOG_SUBSCRIBE))
            {
                Logger::getInstance()->log(LOG_SUBSCRIBE)
                        << "Resending tracked lazy subscription after connection loss to pattern '"
                        << sub.topic << "', to server '" << network_client->getClientId()
                        << ", effective QoS = " << static_cast<int>(sub.qos) << ". Packet ID = " << pack_id.value();
            }
        }

        Logger::getInstance()->log(LOG_INFO)
                << "Resending tracked lazy subscriptions after connection loss: "
                << resendCount << " of " << resendTotal << ". Packet ID = " << pack_id.value();

        // We must have done something, or returned (like when QoS quota done). Otherwise the requeing of the action
        // for the next batch stalls, because SUBACKS do that.
        assert(!subscribes.empty());

        if (!subscribes.empty())
        {
            stageInFlightTrackedSubscriptions(std::move(subscribes), pack_id.value());
            sendInFlightTrackedSubscriptions(network_client.get());
        }

        return;
    }

    std::optional<uint16_t> pack_id;
    std::vector<TrackedSubscriptionMutation> muts;
    muts.reserve(batch_size);

    {
        auto locked = trackedSubscriptionMutations.lock();

        if (locked->empty())
            return;

        size_t batch_entry = 0;
        while (!locked->empty() && ++batch_entry <= batch_size)
        {
            muts.emplace_back(std::move(locked->front()));
            locked->pop_front();
        }
    }

    std::vector<Subscribe> subscribes;

    for (TrackedSubscriptionMutation &mut : muts)
    {
        if (mut.task == TrackedSubscriptionMutationTask::Subscribe)
        {
            bool send_subscription = false;
            const auto emplacement_result = this->trackedSubscriptions->try_emplace(mut.pattern, mut.pattern, mut.qos);
            auto emplacementResultPos = emplacement_result.first.lock();

            if (!emplacementResultPos)
                continue;

            TrackedSubscription &subscription = emplacementResultPos->val;
            subscription.sessions.insert(mut.originatingSession);

            if (emplacement_result.second)
                send_subscription = true;
            else
                send_subscription = mut.qos > subscription.qos;

            if (!send_subscription)
            {
                if (Logger::getInstance()->wouldLog(LOG_SUBSCRIBE))
                {
                    Logger::getInstance()->log(LOG_SUBSCRIBE)
                        << "Subscription already relayed: not relaying subscription by client '"
                        << mut.originatingClientId << "' to pattern '"
                        << mut.pattern << "' to server '" << network_client->getClientId() << ", effective QoS = " << static_cast<int>(mut.qos) << ".";
                }
                continue;
            }

            if (!pack_id)
            {
                pack_id = session->getNextPacketIdLocked();

                if (!pack_id)
                    return;
            }

            if (Logger::getInstance()->wouldLog(LOG_SUBSCRIBE))
            {
                Logger::getInstance()->log(LOG_SUBSCRIBE)
                    << "Relaying subscription by client '" << mut.originatingClientId << "' to pattern '"
                    << mut.pattern << "' to server '" << network_client->getClientId()
                    << ", effective QoS = " << static_cast<int>(mut.qos) << ". Packet ID = " << pack_id.value();
            }

            Subscribe &sub = subscribes.emplace_back(mut.pattern, mut.qos);
            sub.retainAsPublished = true;

            /*
             * Because of the nature of the feature, multiple clients subscribing to one pattern at different QoS levels
             * use the same subscription at the other end. We have to request the retained message because it may now
             * be with a different QoS. This will have the side effect of others getting an unexpected publish with the
             * 'retain' flag on, but that's seemingly unavoidable.
             */
            sub.retainHandling = RetainHandling::SendRetainedMessagesAtSubscribe;
        }
        else if (mut.task == TrackedSubscriptionMutationTask::Unsubscribe)
        {
            auto pos = this->trackedSubscriptions->find(mut.pattern);

            if (pos == this->trackedSubscriptions->end())
                continue;

            auto cur = pos.lock();

            if (!cur)
                continue;

            cur->val.sessions.erase(mut.originatingSession);

            /*
             * We're not removing the entry or unsubscribing here. When incoming subscriptions and unsubscriptions
             * are coming and going, the periodic cleanup will ultimately send the unsubscribe.
             */
        }
    }

    if (!subscribes.empty())
    {
        stageInFlightTrackedSubscriptions(std::move(subscribes), pack_id.value());
        sendInFlightTrackedSubscriptions(network_client.get());
    }
    else
    {
        std::shared_ptr<ThreadData> t = bridgeState->threadData.lock();

        // We have no subacks to retrigger us, so we have to requeue ourselves.
        if (t)
            t->queueProcessTrackedSubscriptionMutations(bridgeState, ProcessTrackedSubscriptionMutationsModifier::Continue);
    }
}

/**
 * @brief Works similar to processTrackedSubscriptionMutations, but then deals with purging tracked subscriptions
 *
 * When processing mutations, we don't actively send unsubscribers (as documented in processTrackedSubscriptionMutations). It
 * can happen that clients keep subscribing and unsubscribing to patterns, so we only send the UNSUBSCRIBE packets
 * when the subscription has been out of use long enough.
 */
void TrackedSubscriptionState::cleanupExpiredTrackedSubscriptions(
    const std::shared_ptr<BridgeState> &bridgeState, const PurgeTrackedSubscriptionModifier modifier)
{
    if (!this->trackedSubscriptions)
        return;

    // When we're still working on resending after connection loss, postpone the purging.
    if (this->curPosResending != this->trackedSubscriptions->end())
        return;

    std::shared_ptr<Session> session = bridgeState->session.lock();

    if (!session)
        return;

    std::shared_ptr<Client> network_client = session->makeSharedClient();

    if (!network_client || !network_client->getAuthenticated())
        return;

    if (modifier == PurgeTrackedSubscriptionModifier::Start)
    {
        if (this->curPosPurging == this->trackedSubscriptions->end())
        {
            Logger::getInstance()->log(LOG_INFO) << "Purging unused tracked lazy subscriptions";

            this->purgeCount = 0;
            this->purgeTotal = this->trackedSubscriptions->size();
            this->curPosPurging = this->trackedSubscriptions->begin();
            this->inFlightTrackedUnsubscriptions.reset();
        }
        else
        {
            return;
        }
    }

    // First cover the retry case. If a UNSUBACK is properly processed, this is always false.
    if (inFlightTrackedUnsubscriptions)
    {
        // That means a stray UNSUBACK caused the requeue of this function, because a matching SUBACK would have
        // cleared the inFlightTrackedUnubscriptions.
        if (modifier != PurgeTrackedSubscriptionModifier::Retry)
            return;

        if (!hasOutdatedInFlightTrackedUnsubscriptions())
            return;

        if (inFlightTrackedUnsubscriptions->tryCount > 3)
        {
            Logger::getInstance()->log(LOG_ERROR)
                    << "Unsubscribe for expired tracked lazy subscription with id " << inFlightTrackedUnsubscriptions->id << " to server '"
                    << network_client->getClientId() << "' went unacknolwleged after " << inFlightTrackedUnsubscriptions->tryCount
                    << " times. Disconnecting";

            inFlightTrackedUnsubscriptions.reset();
            network_client->setDisconnectReason("Missing UNSUBACKs when unsubscribing expired tracked lazy subscriptions");
            ThreadGlobals::getThreadData()->removeClientQueued(network_client);
            return;
        }

        Logger::getInstance()->log(LOG_WARNING)
                << "Missing UNSUBACK when unsubscribing tracked lazy subscription from server '"
                << network_client->getClientId() << "'. Trying again. Try count: " << inFlightTrackedUnsubscriptions->tryCount;

        sendInFlightTrackedUnsubscriptions(network_client.get());
        return;
    }

    std::vector<Unsubscribe> unsubs;
    std::optional<uint16_t> pack_id;

    constexpr size_t batch_size = 100;
    size_t batch_entry = 0;
    while (this->curPosPurging != this->trackedSubscriptions->end() && batch_entry < batch_size)
    {
        auto cur = this->curPosPurging;
        ++this->curPosPurging;
        batch_entry++;
        auto tracked_subscr_pos = cur.lock();

        if (!tracked_subscr_pos)
            continue;

        purgeCount++;

        tracked_subscr_pos->val.purge();

        if (tracked_subscr_pos->val.empty())
        {
            const std::string &filter = tracked_subscr_pos->key;

            if (!pack_id)
            {
                pack_id = session->getNextPacketIdLocked();

                if (!pack_id)
                    return;
            }

            unsubs.emplace_back(filter);
            this->trackedSubscriptions->erase(cur);
        }
    }

    {
        auto log = Logger::getInstance()->log(LOG_INFO);
        log << "Unsubscribing stale lazy subscriptions: checked " << this->purgeCount << " of " << this->purgeTotal
            << " tracked subscriptions.";

        if (!unsubs.empty())
            log << " Sending " << unsubs.size() << " unsubscribes.";
        else
            log << " Done.";
    }

    if (!unsubs.empty())
    {
        stageInFlightTrackedUnsubscriptions(std::move(unsubs), pack_id.value());
        sendInFlightTrackedUnsubscriptions(network_client.get());
    }
    else if (this->curPosPurging != this->trackedSubscriptions->end())
    {
        std::shared_ptr<ThreadData> t = bridgeState->threadData.lock();

        // We have no unsubacks to retrigger us, so we have to requeue ourselves.
        if (t)
            t->queuePurgeStaleTrackedLazySubscriptions(bridgeState, PurgeTrackedSubscriptionModifier::Continue);
    }
}

bool TrackedSubscriptionState::requiresProcessingTrackedSubscriptions()
{
    assert(ThreadGlobals::getThreadData()->thread_id == pthread_self());

    if (trackedSubscriptions && curPosResending != trackedSubscriptions->end())
        return true;

    const bool empty = trackedSubscriptionMutations.lock()->empty();
    return !empty;
}

bool TrackedSubscriptionState::requiresContinuationOfPurging()
{
    assert(ThreadGlobals::getThreadData()->thread_id == pthread_self());

    return trackedSubscriptions && curPosPurging != trackedSubscriptions->end();
}

void TrackedSubscriptionState::removeMatchingInFlightTrackedSubscriptions(uint16_t id)
{
    if (!this->inFlightTrackedSubscriptions)
        return;

    if (this->inFlightTrackedSubscriptions.value().id != id)
        return;

    this->inFlightTrackedSubscriptions.reset();
    return;
}

void TrackedSubscriptionState::removeMatchingInFlightTrackedUnsubscriptions(uint16_t id)
{
    if (!this->inFlightTrackedUnsubscriptions)
        return;

    if (this->inFlightTrackedUnsubscriptions.value().id != id)
        return;

    this->inFlightTrackedUnsubscriptions.reset();
}

bool TrackedSubscriptionState::hasOutdatedInFlightTrackedSubscriptions() const
{
    return this->inFlightTrackedSubscriptions && this->inFlightTrackedSubscriptions->outdated();
}

bool TrackedSubscriptionState::hasOutdatedInFlightTrackedUnsubscriptions() const
{
    return this->inFlightTrackedUnsubscriptions && this->inFlightTrackedUnsubscriptions.value().outdated();
}
