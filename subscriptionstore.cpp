/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "subscriptionstore.h"

#include <cassert>

#include "rwlockguard.h"
#include "retainedmessagesdb.h"
#include "publishcopyfactory.h"
#include "threadglobals.h"
#include "utils.h"
#include "settings.h"
#include "plugin.h"
#include "exceptions.h"
#include "threaddata.h"
#include "globals.h"
#include <deque>

DeferredGetSubscription::DeferredGetSubscription(const std::shared_ptr<SubscriptionNode> &node, const std::string &composedTopic, const bool root) :
    node(node),
    composedTopic(composedTopic),
    root(root)
{

}

ReceivingSubscriber::ReceivingSubscriber(const std::weak_ptr<Session> &ses, uint8_t qos, bool retainAsPublished, const uint32_t subscriptionIdentifier) :
    session(ses.lock()),
    qos(qos),
    retainAsPublished(retainAsPublished),
    subscriptionIdentifier(subscriptionIdentifier)
{

}

SubscriptionNode::SubscriptionNode()
{

}

const std::unordered_map<std::string, Subscription> &SubscriptionNode::getSubscribers() const
{
    return subscribers;
}

std::unordered_map<std::string, SharedSubscribers> &SubscriptionNode::getSharedSubscribers()
{
    return sharedSubscribers;
}

AddSubscriptionType SubscriptionNode::addSubscriber(
    const std::shared_ptr<Session> &subscriber, uint8_t qos, bool noLocal, bool retainAsPublished,
    const std::string &shareName, const uint32_t subscriptionIdentifier)
{
    if (!subscriber)
        return AddSubscriptionType::Invalid;

    Subscription sub;
    sub.session = subscriber;
    sub.qos = qos;
    sub.noLocal = noLocal;
    sub.retainAsPublished = retainAsPublished;
    sub.subscriptionIdentifier = subscriptionIdentifier;

    const std::string &client_id = subscriber->getClientId();
    AddSubscriptionType result = AddSubscriptionType::Invalid;

    std::unique_lock locker(lock);

    lastUpdate = std::chrono::steady_clock::now();

    if (shareName.empty())
    {
        Subscription &s = subscribers[client_id];
        result = s.session.expired() ? AddSubscriptionType::NewSubscription : AddSubscriptionType::ExistingSubscription;
        s = sub;
    }
    else
    {
        SharedSubscribers &subscribers = sharedSubscribers[shareName];
        subscribers.setName(shareName); // c++14 doesn't have try-emplace yet, in which case this separate step wouldn't be needed.

        Subscription &s = subscribers[client_id];
        result = s.session.expired() ? AddSubscriptionType::NewSubscription : AddSubscriptionType::ExistingSubscription;
        s = sub;
    }

    return result;
}

void SubscriptionNode::removeSubscriber(const std::shared_ptr<Session> &subscriber, const std::string &shareName)
{
    Subscription sub;
    sub.session = subscriber;
    sub.qos = 0;

    const std::string &clientId = subscriber->getClientId();

    std::unique_lock locker(lock);

    lastUpdate = std::chrono::steady_clock::now();

    if (shareName.empty())
    {
        auto it = subscribers.find(clientId);

        if (it != subscribers.end())
        {
            subscribers.erase(it);
        }
    }
    else
    {
        auto pos = sharedSubscribers.find(shareName);
        if (pos != sharedSubscribers.end())
        {
            SharedSubscribers &subscribers = pos->second;
            subscribers.erase(clientId);
        }
    }
}

SubscriptionStore::SubscriptionStore() :
    sessionsByIdConst(sessionsById)
{

}

/**
 * @brief SubscriptionStore::getDeepestNode gets the node in the tree walking the path of 'the/subscription/topic/path', making new nodes as required.
 * @param topic
 * @param subtopics
 * @return
 */
std::shared_ptr<SubscriptionNode> SubscriptionStore::getDeepestNode(const std::vector<std::string> &subtopics, bool abort_on_dead_end)
{
    const std::shared_ptr<SubscriptionNode> *start = &root;
    if (!subtopics.empty())
    {
        const std::string &first = subtopics.front();
        if (first.length() > 0 && first[0] == '$')
            start = &rootDollar;
    }

    std::shared_ptr<SubscriptionNode> result;
    bool retry_mode = false;

    for(int i = 0; i < 2; i++)
    {
        assert(i < 1 || retry_mode);

        std::shared_lock rlock(subscriptions_lock, std::defer_lock);
        std::unique_lock wlock(subscriptions_lock, std::defer_lock);

        if (retry_mode)
        {
            assert(!abort_on_dead_end);
            wlock.lock();
        }
        else
            rlock.lock();

        auto subtopic_pos = subtopics.begin();
        const std::shared_ptr<SubscriptionNode> *deepestNode = start;

        while(subtopic_pos != subtopics.end())
        {
            const std::string &subtopic = *subtopic_pos;
            std::shared_ptr<SubscriptionNode> *selectedChildren = nullptr;

            if (subtopic == "#")
                selectedChildren = &(*deepestNode)->childrenPound;
            else if (subtopic == "+")
                selectedChildren = &(*deepestNode)->childrenPlus;
            else
            {
                auto &children = (*deepestNode)->children;

                if (retry_mode)
                {
                    assert(wlock.owns_lock());
                    selectedChildren = &children[subtopic];
                }
                else // read-only path
                {
                    assert(rlock.owns_lock());
                    auto child_pos = children.find(subtopic);

                    if (child_pos == children.end())
                    {
                        if (abort_on_dead_end)
                            return result;

                        retry_mode = true;
                        break;
                    }
                    else
                    {
                        selectedChildren = &child_pos->second;
                    }
                }
            }

            std::shared_ptr<SubscriptionNode> &node = *selectedChildren;

            if (!node)
            {
                if (!retry_mode)
                {
                    assert(rlock.owns_lock());

                    if (abort_on_dead_end)
                        return result;

                    retry_mode = true;
                    break;
                }

                assert(retry_mode);
                assert(wlock.owns_lock());
                node = std::make_shared<SubscriptionNode>();

                /*
                 * This is not technically correct, because we haven't made a subscription yet, but it:
                 *
                 * 1) Only happens when we're about to make a subscription, and not when we remove one.
                 * 2) Allows continuous subscription and unsubcription on a node without needing to update this atomic counter.
                 */
                subscriptionCount++;
            }

            deepestNode = &node;
            subtopic_pos++;
        }

        assert(deepestNode);
        assert(*deepestNode);

        if (retry_mode && subtopic_pos != subtopics.end())
            continue;

        result = *deepestNode;
        assert(result);
        return result;
    }

    return result;
}

AddSubscriptionType SubscriptionStore::addSubscription(
    std::shared_ptr<Client> &client, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished,
    uint32_t subscriptionIdentifier)
{
    const static std::string empty;
    return addSubscription(client, subtopics, qos, noLocal, retainAsPublished, empty, subscriptionIdentifier);
}

AddSubscriptionType SubscriptionStore::addSubscription(
    std::shared_ptr<Client> &client, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished,
    const std::string &shareName, const uint32_t subscriptionIdentifier)
{
    const std::shared_ptr<SubscriptionNode> deepestNode = getDeepestNode(subtopics);

    if (!deepestNode)
        return AddSubscriptionType::Invalid;

    return deepestNode->addSubscriber(client->getSession(), qos, noLocal, retainAsPublished, shareName, subscriptionIdentifier);
}

AddSubscriptionType SubscriptionStore::addSubscription(
    const std::shared_ptr<Session> &session, const std::string &topicFilter, uint8_t qos, bool noLocal, bool retainAsPublished,
    const uint32_t subscriptionIdentifier)
{
    if (!session) return AddSubscriptionType::Invalid;

    std::vector<std::string> subtopics = splitTopic(topicFilter);

    std::string shareName;
    parseSubscriptionShare(subtopics, shareName);

    const std::shared_ptr<SubscriptionNode> deepestNode = getDeepestNode(subtopics);

    if (!deepestNode)
        return AddSubscriptionType::Invalid;

    return deepestNode->addSubscriber(session, qos, noLocal, retainAsPublished, shareName, subscriptionIdentifier);
}

void SubscriptionStore::removeSubscription(const std::shared_ptr<Session> &session, const std::string &topic)
{
    if (!session)
        return;

    std::vector<std::string> subtopics = splitTopic(topic);

    std::string shareName;
    parseSubscriptionShare(subtopics, shareName);

    std::shared_ptr<SubscriptionNode> node = getDeepestNode(subtopics, true);

    if (!node)
        return;

    node->removeSubscriber(session, shareName);
}

void SubscriptionStore::removeSubscription(std::shared_ptr<Client> &client, const std::string &topic)
{
    if (!client)
        return;

    std::shared_ptr<Session> session = client->getSession();
    removeSubscription(session, topic);
}

std::shared_ptr<Session> SubscriptionStore::getBridgeSession(std::shared_ptr<Client> &client)
{
    const std::string &client_id = client->getClientId();

    std::unique_lock locker(sessions_lock);

    std::shared_ptr<Session> &session = sessionsById[client_id];

    if (!session)
        session = std::make_shared<Session>(client_id, client->getUsername());

    session->assignActiveConnection(client);
    client->assignSession(session);
    session->setClientType(ClientType::LocalBridge);
    return session;
}

/**
 * @brief SubscriptionStore::registerClientAndKickExistingOne registers a client with previously set parameters for the session.
 * @param client
 *
 * Under normal MQTT operation, the 'if' clause is always used. The 'else' is only in (fuzz) testing and other rare conditions.
 */
void SubscriptionStore::registerClientAndKickExistingOne(std::shared_ptr<Client> &client)
{
    const std::unique_ptr<StowedClientRegistrationData> &registrationData = client->getRegistrationData();

    if (registrationData)
    {
        registerClientAndKickExistingOne(client, registrationData->clean_start, registrationData->clientReceiveMax, registrationData->sessionExpiryInterval);
        client->clearRegistrationData();
    }
    else
    {
        const Settings *settings = ThreadGlobals::getSettings();
        registerClientAndKickExistingOne(client, true, settings->maxQosMsgPendingPerClient, settings->expireSessionsAfterSeconds);
    }
}

// Removes an existing client when it already exists [MQTT-3.1.4-2].
void SubscriptionStore::registerClientAndKickExistingOne(std::shared_ptr<Client> &client, bool clean_start, uint16_t clientReceiveMax, uint32_t sessionExpiryInterval)
{
    ThreadGlobals::getThreadData()->queueClientNextKeepAliveCheck(client, true);

    // These destructors need to be called outside the sessions lock, so placing here.
    std::shared_ptr<Session> session;

    if (client->getClientId().empty())
        throw ProtocolError("Trying to store client without an ID.", ReasonCodes::ProtocolError);

    {
        std::unique_lock ses_locker(sessions_lock);

        auto session_it = sessionsById.find(client->getClientId());
        if (session_it != sessionsById.end())
        {
            session = session_it->second;

            if (session)
            {
                if (session->getUsername() != client->getUsername())
                    throw ProtocolError("Cannot take over session with different username", ReasonCodes::NotAuthorized);

                std::shared_ptr<Client> clientOfOtherSession = session->makeSharedClient();

                if (clientOfOtherSession)
                {
                    logger->logf(LOG_NOTICE, "Disconnecting existing client with id '%s'", clientOfOtherSession->getClientId().c_str());
                    std::shared_ptr<ThreadData> td = clientOfOtherSession->lockThreadData();
                    td->serverInitiatedDisconnect(std::move(clientOfOtherSession), ReasonCodes::SessionTakenOver, "Another client with this ID connected");
                }

            }
        }

        if (!session || session->getDestroyOnDisconnect() || clean_start)
        {
            // Don't use sdt::make_shared to avoid the weak pointers from retaining the size of session in the control block.
            session = std::shared_ptr<Session>(new Session(client->getClientId(), client->getUsername()));

            sessionsById[client->getClientId()] = session;
        }
    }

    session->assignActiveConnection(session, client, clientReceiveMax, sessionExpiryInterval, clean_start);
}

/**
 * @brief SubscriptionStore::lockSession returns the session if it exists. Returning is done keep the shared pointer active, to
 * avoid race conditions with session removal.
 * @param clientid
 * @return
 */
std::shared_ptr<Session> SubscriptionStore::lockSession(const std::string &clientid)
{
    std::shared_lock ses_locker(sessions_lock);

    auto it = sessionsByIdConst.find(clientid);
    if (it != sessionsByIdConst.end())
    {
        return it->second;
    }
    return std::shared_ptr<Session>();
}

void SubscriptionStore::sendWill(const std::shared_ptr<WillPublish> will, const std::shared_ptr<Session> session, const std::string &log)
{
    if (!will || !session)
        return;

    /*
     * Avoid sending two immediate wills when a session is destroyed with the client disconnect.
     * Session is null when you're destroying a client before a session is assigned, or
     * when an old client has no session anymore after a client with the same ID connects.
     */
    session->clearWill();

    const Settings *settings = ThreadGlobals::getSettings();

    if (!settings->willsEnabled)
        return;

    logger->log(LOG_DEBUG) << log << " " << will->topic;

    Authentication &auth = *ThreadGlobals::getAuth();
    const AuthResult authResult = auth.aclCheck(*will, will->payload, AclAccess::write);

    if (authResult == AuthResult::success || authResult == AuthResult::success_without_setting_retained)
    {
        PublishCopyFactory factory(will.get());
        queuePacketAtSubscribers(factory, will->client_id);

        if (will->retain && authResult == AuthResult::success)
            setRetainedMessage(*will, will->getSubtopics());
    }
}

/**
 * @brief SubscriptionStore::sendQueuedWillMessages sends queued will messages.
 *
 * The expiry interval as set in the properties of the will message is not used to check for expiration here. To
 * quote the specs: "If present, the Four Byte value is the lifetime of the Will Message in seconds and is sent as
 * the Publication Expiry Interval when the Server publishes the Will Message."
 *
 * If a new Network Connection to this Session is made before the Will Delay Interval has passed, the Server
 * MUST NOT send the Will Message [MQTT-3.1.3-9].
 */
void SubscriptionStore::sendQueuedWillMessages()
{
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::seconds secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    std::lock_guard<std::mutex> locker(this->pendingWillsMutex);

    auto it = pendingWillMessages.begin();
    while (it != pendingWillMessages.end())
    {
        const std::chrono::seconds &sendAt = it->first;

        if (sendAt > secondsSinceEpoch)
            break;

        std::vector<QueuedWill> &willsOfSlot = it->second;

        for(QueuedWill &will : willsOfSlot)
        {
            std::shared_ptr<WillPublish> p = will.getWill().lock();
            std::shared_ptr<Session> s = will.getSession();

            // If the session has been picked up again after the will was originally queued, we should not send it.
            if (s && s->hasActiveClient())
                continue;

            sendWill(p, s, "Sending delayed will on topic: ");
        }

        it = pendingWillMessages.erase(it);
    }
}

void SubscriptionStore::queueOrSendWillMessage(
    const std::shared_ptr<WillPublish> &willMessage, const std::shared_ptr<Session> &session, bool forceNow)
{
    if (!willMessage)
        return;

    const uint32_t delay = forceNow ? 0 : willMessage->will_delay;

    if (delay > 0)
        queueWillMessage(willMessage, session);
    else
        sendWill(willMessage, session, "Sending immediate will on topic: ");
}

/**
 * @brief SubscriptionStore::queueWillMessage queues the will message in a sorted map.
 *
 * The queued will is only valid for that time. Should a new will be placed in the map for a session, the original shared_ptr
 * will be cleared and the previously queued entry is void (but still there, so it needs to be checked).
 */
void SubscriptionStore::queueWillMessage(const std::shared_ptr<WillPublish> &willMessage, const std::shared_ptr<Session> &session)
{
    if (!willMessage)
        return;

    logger->log(LOG_DEBUG) << "Queueing will on topic '" << willMessage->topic << "', with delay of " << willMessage->will_delay << " seconds.";

    willMessage->setQueuedAt();

    QueuedWill queuedWill(willMessage, session);
    const std::chrono::time_point<std::chrono::steady_clock> sendWillAt = std::chrono::steady_clock::now() + std::chrono::seconds(willMessage->will_delay);
    std::chrono::seconds secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(sendWillAt.time_since_epoch());

    std::lock_guard<std::mutex> locker(this->pendingWillsMutex);
    this->pendingWillMessages[secondsSinceEpoch].push_back(queuedWill);
}

void SubscriptionStore::publishNonRecursively(
    SubscriptionNode *this_node, std::vector<ReceivingSubscriber> &targetSessions, const std::string &senderClientId) noexcept
{
    std::shared_lock locker(this_node->lock);

    for (auto &pair : this_node->subscribers)
    {
        const Subscription &sub = pair.second;
        targetSessions.emplace_back(sub.session, sub.qos, sub.retainAsPublished, sub.subscriptionIdentifier);

        /*
         * Shared pointer expires when session has been cleaned by 'clean session' disconnect.
         *
         * By not using a tempory locked shared_ptr<Session> for checks, doing an optimistic insertion instead, we avoid
         * unnecessary copies. The only extra overhead this causes is the list pop when we decice to remove it.
         */
        if (!targetSessions.back().session)
        {
            targetSessions.pop_back();
            continue;
        }

        if (sub.noLocal && targetSessions.back().session->getClientId() == senderClientId)
        {
            targetSessions.pop_back();
            continue;
        }
    }

    if (this_node->sharedSubscribers.empty())
        return;

    const Settings *settings = ThreadGlobals::getSettings();

    for(auto &pair : this_node->sharedSubscribers)
    {
        SharedSubscribers &subscribers = pair.second;

        const Subscription *sub = nullptr;

        if (settings->sharedSubscriptionTargeting == SharedSubscriptionTargeting::SenderHash)
        {
            const size_t hash = std::hash<std::string>()(senderClientId);
            sub = subscribers.getNext(hash);
        }
        else if (settings->sharedSubscriptionTargeting == SharedSubscriptionTargeting::RoundRobin)
            sub = subscribers.getNext();
        else if (settings->sharedSubscriptionTargeting == SharedSubscriptionTargeting::First)
            sub = subscribers.getFirst();

        if (sub == nullptr)
            continue;

        // Same comment about duplicate copies as above.
        targetSessions.emplace_back(sub->session, sub->qos, sub->retainAsPublished, sub->subscriptionIdentifier);
        if (!targetSessions.back().session)
        {
            targetSessions.pop_back();
            continue;
        }

        /*
         * We don't filter out 'no local' subscriptions:
         *
         * It is a Protocol Error to set the No Local bit to 1 on a Shared Subscription [MQTT-3.8.3-4]
         */
    }
}

/**
 * @brief SubscriptionStore::publishRecursively
 * @param cur_subtopic_it
 * @param end
 * @param this_node
 * @param packet
 * @param count as a reference (vs return value) because a return value introduces an extra call i.e. limits tail recursion optimization.
 *
 * As noted in the params section, this method was written so that it could be (somewhat) optimized for tail recursion by the compiler. If you refactor this,
 * look at objdump --disassemble --demangle to see how many calls (not jumps) to itself are made and compare.
 */
void SubscriptionStore::publishRecursively(
    std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
    SubscriptionNode *this_node, std::vector<ReceivingSubscriber> &targetSessions,
    const std::string &senderClientId) noexcept
{
    if (cur_subtopic_it == end) // This is the end of the topic path, so look for subscribers here.
    {
        if (this_node)
        {
            publishNonRecursively(this_node, targetSessions, senderClientId);

            // Subscribing to 'one/two/three/#' also gives you 'one/two/three'.
            if (this_node->childrenPound)
            {
                publishNonRecursively(this_node->childrenPound.get(), targetSessions, senderClientId);
            }
        }
        return;
    }

    // Null nodes in the tree shouldn't happen at this point. It points to bugs elsewhere. However, I don't remember why I check the pointer
    // inside the if-block above, instead of the start of the method.
    assert(this_node != nullptr);

    if (this_node->children.empty() && !this_node->childrenPlus && !this_node->childrenPound)
        return;

    const std::string &cur_subtop = *cur_subtopic_it;

    const auto next_subtopic = ++cur_subtopic_it;

    if (this_node->childrenPound)
    {
        publishNonRecursively(this_node->childrenPound.get(), targetSessions, senderClientId);
    }

    const auto &sub_node = this_node->children.find(cur_subtop);

    if (this_node->childrenPlus)
    {
        publishRecursively(next_subtopic, end, this_node->childrenPlus.get(), targetSessions, senderClientId);
    }

    if (sub_node != this_node->children.end())
    {
        publishRecursively(next_subtopic, end, sub_node->second.get(), targetSessions, senderClientId);
    }
}

void SubscriptionStore::queuePacketAtSubscribers(PublishCopyFactory &copyFactory, const std::string &senderClientId, bool dollar)
{
    /*
     * Sometimes people publish or set as will topics with dollar. Node-to-Node communication for bridges for instance.
     * We still accept them, but just decide to do nothing with them. Only the FlashMQ internals are allowed to publish
     * on dollar topics.
     */
    if (!dollar && copyFactory.getTopic()[0] == '$') // String is always 0-terminated, so we can access first element.
    {
        return;
    }

    SubscriptionNode *startNode = dollar ? rootDollar.get() : root.get();

    const size_t reserve = this->subscriber_reserve.load(std::memory_order_relaxed);
    std::vector<ReceivingSubscriber> subscriberSessions;
    subscriberSessions.reserve(reserve);

    {
        const std::vector<std::string> &subtopics = copyFactory.getSubtopics();
        std::shared_lock locker(subscriptions_lock);
        publishRecursively(subtopics.begin(), subtopics.end(), startNode, subscriberSessions, senderClientId);
    }

    if (subscriberSessions.size() > reserve && subscriberSessions.size() <= 1048576)
        this->subscriber_reserve.store(reserve, std::memory_order_relaxed);

    for(const ReceivingSubscriber &x : subscriberSessions)
    {
        x.session->writePacket(copyFactory, x.qos, x.retainAsPublished, x.subscriptionIdentifier);
    }
}

void SubscriptionStore::giveClientRetainedMessagesRecursively(std::vector<std::string>::const_iterator cur_subtopic_it,
                                                              std::vector<std::string>::const_iterator end,
                                                              const std::shared_ptr<RetainedMessageNode> &this_node,
                                                              bool poundMode,
                                                              const std::shared_ptr<Session> &session, const uint8_t max_qos,
                                                              const uint32_t subscription_identifier,
                                                              const std::chrono::time_point<std::chrono::steady_clock> &limit,
                                                              std::deque<DeferredRetainedMessageNodeDelivery> &deferred,
                                                              int &drop_count, int &processed_nodes_count)
{
    if (!this_node)
        return;

    if (cur_subtopic_it == end)
    {
        Authentication &auth = *ThreadGlobals::getAuth();

        std::lock_guard<std::mutex> locker(this_node->messageSetMutex);

        if (this_node->message)
        {
            const RetainedMessage &rm = *this_node->message;

            if (!rm.hasExpired()) // We can't also erase here, because we're operating under a read lock.
            {
                Publish publish = rm.publish;
                if (auth.aclCheck(publish, publish.payload) == AuthResult::success)
                {
                    PublishCopyFactory copyFactory(&publish);
                    const PacketDropReason drop_reason = session->writePacket(copyFactory, max_qos, true, subscription_identifier);

                    if (drop_reason == PacketDropReason::BufferFull || drop_reason == PacketDropReason::QoSTODOSomethingSomething)
                    {
                        drop_count++;

                        DeferredRetainedMessageNodeDelivery d;
                        d.node = this_node;
                        d.cur = cur_subtopic_it;
                        d.end = end;
                        d.poundMode = poundMode;
                        deferred.push_back(d);

                        /*
                         * Capacity-wise, we wouldn't have to return here if it was merely a QoS limit, but then it becomes hard
                         * to filter out the children of this node, which you'd need to avoid duplicate deliveries. So,
                         * this is the easier approach.
                         */
                        return;
                    }
                }
            }
        }

        if (poundMode)
        {
            const int drop_count_start = drop_count;

            for (auto &pair : this_node->children)
            {
                if (std::chrono::steady_clock::now() >= limit || drop_count_start != drop_count)
                {
                    DeferredRetainedMessageNodeDelivery d;
                    d.node = pair.second;
                    d.cur = cur_subtopic_it;
                    d.end = end;
                    d.poundMode = poundMode;
                    deferred.push_back(d);
                }
                else
                {
                    std::shared_ptr<RetainedMessageNode> &child = pair.second;
                    giveClientRetainedMessagesRecursively(
                        cur_subtopic_it, end, child, poundMode, session, max_qos, subscription_identifier, limit, deferred, drop_count, ++processed_nodes_count);
                }
            }
        }

        return;
    }

    const std::string &cur_subtop = *cur_subtopic_it;
    const auto next_subtopic = ++cur_subtopic_it;

    if (cur_subtop == "#")
    {
        // We start at this node, so that a subscription on 'one/two/three/#' gives you 'one/two/three' too.
        giveClientRetainedMessagesRecursively(
            next_subtopic, end, this_node, true, session, max_qos, subscription_identifier, limit, deferred, drop_count, ++processed_nodes_count);
    }
    else if (cur_subtop == "+")
    {
        const int drop_count_start = drop_count;

        for (std::pair<const std::string, std::shared_ptr<RetainedMessageNode>> &pair : this_node->children)
        {
            if (std::chrono::steady_clock::now() >= limit || drop_count_start != drop_count)
            {
                DeferredRetainedMessageNodeDelivery d;
                d.node = pair.second;
                d.cur = next_subtopic;
                d.end = end;
                d.poundMode = poundMode;
                deferred.push_back(d);
            }
            else
            {
                std::shared_ptr<RetainedMessageNode> &child = pair.second;
                giveClientRetainedMessagesRecursively(
                    next_subtopic, end, child, false, session, max_qos, subscription_identifier, limit, deferred, drop_count, ++processed_nodes_count);
            }
        }
    }
    else
    {
        std::shared_ptr<RetainedMessageNode> children = this_node->getChildren(cur_subtop);

        if (children)
        {
            if (std::chrono::steady_clock::now() >= limit)
            {
                DeferredRetainedMessageNodeDelivery d;
                d.node = children;
                d.cur = next_subtopic;
                d.end = end;
                d.poundMode = poundMode;
                deferred.push_back(d);
            }
            else
            {
                giveClientRetainedMessagesRecursively(
                    next_subtopic, end, children, false, session, max_qos, subscription_identifier, limit, deferred, drop_count, ++processed_nodes_count);
            }
        }
    }
}

void SubscriptionStore::giveClientRetainedMessagesInitiateDeferred(const std::weak_ptr<Session> ses,
                                                                   const std::shared_ptr<const std::vector<std::string>> subscribeSubtopicsCopy,
                                                                   std::shared_ptr<std::deque<DeferredRetainedMessageNodeDelivery>> deferred,
                                                                   int &requeue_count, uint &total_node_count, uint8_t max_qos,
                                                                   const uint32_t subscription_identifier)
{
    std::shared_ptr<Session> session = ses.lock();

    if (!session)
        return;

    const Settings *settings = ThreadGlobals::getSettings();

    const std::chrono::time_point<std::chrono::steady_clock> new_limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
    int drop_count = 0;
    int processed_nodes = 0;

    for (; !deferred->empty(); deferred->pop_front())
    {
        if (std::chrono::steady_clock::now() >= new_limit)
            break;

        if (drop_count > 0)
            break;

        DeferredRetainedMessageNodeDelivery &d = deferred->front();

        std::shared_ptr<RetainedMessageNode> node = d.node.lock();

        if (!node)
            continue;

        RWLockGuard locker(&retainedMessagesRwlock);
        locker.rdlock();
        giveClientRetainedMessagesRecursively(d.cur, d.end, node, d.poundMode, session, max_qos, subscription_identifier, new_limit, *deferred, drop_count, processed_nodes);
    }

    total_node_count += processed_nodes;

    if (processed_nodes > 0)
    {
        requeue_count = 0;
    }

    if (!deferred->empty() && ++requeue_count < 100 && total_node_count < settings->retainedMessagesNodeLimit)
    {
        ThreadData *t = ThreadGlobals::getThreadData();
        auto again = std::bind(&SubscriptionStore::giveClientRetainedMessagesInitiateDeferred, this,
                               ses, subscribeSubtopicsCopy, deferred, requeue_count, total_node_count, max_qos, subscription_identifier);

        /*
         * Adding a delayed retry is kind of a cheap way to avoid detecting when there is buffer or QoS space in the event loop, but that comes
         * with several layers of complexity that makes the the normal (non-retained) flow more complicated. Also, buffer full situations
         * is probably most likely to happen when clients subscribe to a broad wildcard on a big subscription tree, and this
         * allows some depriorirzation.
         */
        if (drop_count > 0)
            t->addDelayedTask(again, 50);
        else
            t->addImmediateTask(again);
    }
}

void SubscriptionStore::giveClientRetainedMessages(const std::shared_ptr<Session> &ses,
                                                   const std::vector<std::string> &subscribeSubtopics, uint8_t max_qos, const uint32_t subscriptionIdentifier)
{
    if (!ses)
        return;

    const Settings *settings = ThreadGlobals::getSettings();

    if (settings->retainedMessagesMode >= RetainedMessagesMode::EnabledWithoutRetaining)
        return;

    // The specs aren't clear whether retained messages should be dropped, or just have their retain flag stripped. I chose the former,
    // otherwise clients have no way of knowing if a message was retained or not.
    {
        const std::shared_ptr<Client> client = ses->makeSharedClient();

        if (client && !client->isRetainedAvailable())
            return;
    }

    const std::shared_ptr<RetainedMessageNode> *startNode = &retainedMessagesRoot;
    if (!subscribeSubtopics.empty() && !subscribeSubtopics[0].empty() > 0 && subscribeSubtopics[0][0] == '$')
        startNode = &retainedMessagesRootDollar;

    const std::shared_ptr<const std::vector<std::string>> subscribeSubtopicsCopy = std::make_shared<const std::vector<std::string>>(subscribeSubtopics);
    std::shared_ptr<std::deque<DeferredRetainedMessageNodeDelivery>> deferred = std::make_shared<std::deque<DeferredRetainedMessageNodeDelivery>>();

    DeferredRetainedMessageNodeDelivery start;
    start.node = *startNode;
    start.cur = subscribeSubtopicsCopy->begin();
    start.end = subscribeSubtopicsCopy->end();

    deferred->push_back(start);

    int requeue_count = 0;
    uint total_node_count = 0;
    giveClientRetainedMessagesInitiateDeferred(ses, subscribeSubtopicsCopy, deferred, requeue_count, total_node_count, max_qos, subscriptionIdentifier);
}

/**
 * @brief SubscriptionStore::trySetRetainedMessages queues setting of retained messages if not able to set directly.
 * @param publish
 * @param subtopics
 */
void SubscriptionStore::trySetRetainedMessages(const Publish &publish, const std::vector<std::string> &subtopics)
{
    const Settings *settings = ThreadGlobals::getSettings();
    const bool try_lock_fail = settings->setRetainedMessageDeferTimeout.count() != 0;

    ThreadData *td = ThreadGlobals::getThreadData();

    if (!td)
        return;

    td->retainedMessageSet.inc(1);

    // Only do direct setting when there are none queued, to avoid out of order races, which would result in the wrong ultimate value.
    if (td->queuedRetainedMessagesEmpty() && setRetainedMessage(publish, subtopics, try_lock_fail))
        return;

    const std::chrono::milliseconds spread(td->randomish() % settings->setRetainedMessageDeferTimeoutSpread.count());
    std::chrono::time_point<std::chrono::steady_clock> limit = std::chrono::steady_clock::now() + settings->setRetainedMessageDeferTimeout + spread;
    td->queueSettingRetainedMessage(publish, subtopics, limit);
}

bool SubscriptionStore::setRetainedMessage(const Publish &publish, const std::vector<std::string> &subtopics, bool try_lock_fail)
{
    assert(!subtopics.empty());

    const Settings *settings = ThreadGlobals::getSettings();

    if (settings->retainedMessagesMode >= RetainedMessagesMode::EnabledWithoutRetaining)
        return true;

    const std::shared_ptr<RetainedMessageNode> *deepestNode = &retainedMessagesRoot;
    if (!subtopics.empty() && !subtopics[0].empty() > 0 && subtopics[0][0] == '$')
        deepestNode = &retainedMessagesRootDollar;

    bool needsWriteLock = false;
    auto subtopic_pos = subtopics.begin();
    std::shared_ptr<RetainedMessageNode> selected_node;
    std::shared_ptr<RetainedMessageNode> retry_point;

    // First do a read-only search for the node.
    {
        RWLockGuard locker(&retainedMessagesRwlock);
        if (try_lock_fail)
        {
            if (!locker.tryrdlock())
                return false;
        }
        else
            locker.rdlock();

        while(subtopic_pos != subtopics.end())
        {
            auto pos = (*deepestNode)->children.find(*subtopic_pos);

            if (pos == (*deepestNode)->children.end())
            {
                needsWriteLock = true;
                retry_point = *deepestNode;
                break;
            }

            std::shared_ptr<RetainedMessageNode> &selectedChildren = pos->second;

            if (!selectedChildren)
            {
                needsWriteLock = true;
                retry_point = *deepestNode;
                break;
            }
            deepestNode = &selectedChildren;
            subtopic_pos++;
        }

        assert(deepestNode);

        if (!needsWriteLock && deepestNode)
        {
            selected_node = *deepestNode;
        }
    }

    if (needsWriteLock)
    {
        RWLockGuard locker(&retainedMessagesRwlock);
        if (try_lock_fail)
        {
            if (!locker.trywrlock())
                return false;
        }
        else
            locker.wrlock();

        deepestNode = &retry_point;

        while(subtopic_pos != subtopics.end())
        {
            std::shared_ptr<RetainedMessageNode> &selectedChildren = (*deepestNode)->children[*subtopic_pos];

            if (!selectedChildren)
            {
                selectedChildren = std::make_shared<RetainedMessageNode>();
            }
            deepestNode = &selectedChildren;
            subtopic_pos++;
        }

        assert(deepestNode);

        if (deepestNode)
        {
            selected_node = *deepestNode;
        }
    }

    if (selected_node)
    {
        const ssize_t diff = selected_node->addPayload(publish);

        if (diff != 0)
            this->retainedMessageCount.fetch_add(diff);
    }

    return true;
}

// Clean up the weak pointers to sessions and remove nodes that are empty.
int SubscriptionNode::cleanSubscriptions(std::deque<std::weak_ptr<SubscriptionNode>> &defferedLeafs, size_t &real_subscriber_count)
{
    const size_t children_amount = children.size();
    const bool split = children_amount > 15;

    int subscribersLeftInChildren = 0;
    auto childrenIt = children.begin();
    while(childrenIt != children.end())
    {
        std::shared_ptr<SubscriptionNode> &node = childrenIt->second;

        if (!node)
            continue;

        if (split && !node->empty())
        {
            defferedLeafs.push_back(node);
            subscribersLeftInChildren += 1; // We just have to be sure it's not 0.
            childrenIt++;
            continue;
        }

        int n = node->cleanSubscriptions(defferedLeafs, real_subscriber_count);
        subscribersLeftInChildren += n;

        if (n > 0)
            childrenIt++;
        else
            childrenIt = children.erase(childrenIt);
    }

    std::list<std::shared_ptr<SubscriptionNode>*> wildcardChildren;
    wildcardChildren.push_back(&childrenPlus);
    wildcardChildren.push_back(&childrenPound);

    for (std::shared_ptr<SubscriptionNode> *node : wildcardChildren)
    {
        std::shared_ptr<SubscriptionNode> &node_ = *node;

        if (!node_)
            continue;
        int n = node_->cleanSubscriptions(defferedLeafs, real_subscriber_count);
        subscribersLeftInChildren += n;

        if (n == 0)
        {
            Logger::getInstance()->logf(LOG_DEBUG, "Resetting wildcard children");
            node_.reset();
        }
    }

    {
        // This is not particularlly fast when it's many items. But we don't do it often, so is probably okay.
        auto it = subscribers.begin();
        while (it != subscribers.end())
        {
            auto cur_it = it;
            it++;

            if (cur_it->second.session.expired())
            {
                Logger::getInstance()->logf(LOG_DEBUG, "Removing empty spot in subscribers map");
                subscribers.erase(cur_it);
            }
        }
    }

    {
        auto shared_it = sharedSubscribers.begin();
        while (shared_it != sharedSubscribers.end())
        {
            auto cur_shared = shared_it;
            shared_it++;

            SharedSubscribers &subscribers_of_share = cur_shared->second;
            subscribers_of_share.purgeAndReIndex();

            if (subscribers_of_share.empty())
                sharedSubscribers.erase(cur_shared);
        }
    }

    const Settings *settings = ThreadGlobals::getSettings();
    const bool grace_period_expired = lastUpdate + settings->subscriptionNodeLifetime < std::chrono::steady_clock::now();
    const int grace_period_fake = static_cast<int>(!grace_period_expired);
    const size_t node_subscriber_count = subscribers.size() + sharedSubscribers.size();
    real_subscriber_count += node_subscriber_count;
    return node_subscriber_count + subscribersLeftInChildren + grace_period_fake;
}

bool SubscriptionNode::empty() const
{
    return children.empty() && subscribers.empty() && sharedSubscribers.empty() && !childrenPlus && !childrenPound;
}

void SubscriptionStore::removeSession(const std::shared_ptr<Session> &session)
{
    if (!session)
        return;

    const std::string &clientid = session->getClientId();
    logger->log(LOG_DEBUG) << "Removing session of client '" << clientid << "', if it matches the object.";

    std::list<std::shared_ptr<Session>> sessionsToRemove;

    {
        std::unique_lock session_locker(sessions_lock);

        auto session_it = sessionsById.find(clientid);
        if (session_it != sessionsById.end() && session_it->second == session)
        {
            sessionsToRemove.push_back(session_it->second);
            sessionsById.erase(session_it);
        }
    }

    for(std::shared_ptr<Session> &s : sessionsToRemove)
    {
        if (!s)
            continue;

        std::shared_ptr<WillPublish> will = s->getWill();
        if (will)
        {
            queueOrSendWillMessage(will, s, true);
        }

        s.reset();
    }
}

/**
 * @brief SubscriptionStore::removeExpiredSessionsClients removes expired sessions.
 *
 * For Mqtt3 this is non-standard, but the standard doesn't keep real world constraints into account.
 */
void SubscriptionStore::removeExpiredSessionsClients()
{
    logger->logf(LOG_DEBUG, "Cleaning out old sessions");

    const std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    const std::chrono::seconds secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());

    // Collect sessions to remove for a separate step, to avoid holding two locks at the same time.
    std::vector<std::shared_ptr<Session>> sessionsToRemove;

    int removedSessions = 0;
    int processedRemovals = 0;
    int queuedRemovalsLeft = -1;

    {
        std::lock_guard<std::mutex> locker(this->queuedSessionRemovalsMutex);

        auto it = queuedSessionRemovals.begin();
        while (it != queuedSessionRemovals.end())
        {
            const std::chrono::seconds &removeAt = it->first;

            if (removeAt > secondsSinceEpoch)
            {
                break;
            }

            std::vector<std::weak_ptr<Session>> &sessionsFromSlot = it->second;

            for (std::weak_ptr<Session> &ses : sessionsFromSlot)
            {
                std::shared_ptr<Session> lockedSession = ses.lock();

                // A session could have been picked up again, so we have to verify its expiration status.
                if (lockedSession && !lockedSession->hasActiveClient())
                {
                    sessionsToRemove.push_back(lockedSession);
                }
            }
            it = queuedSessionRemovals.erase(it);

            processedRemovals++;
        }

        queuedRemovalsLeft = queuedSessionRemovals.size();
    }

    for(std::shared_ptr<Session> &session : sessionsToRemove)
    {
        removeSession(session);
        removedSessions++;
    }

    logger->logf(LOG_DEBUG, "Processed %d queued session removals, resulting in %d deleted expired sessions. %d queued removals in the future.",
                 processedRemovals, removedSessions, queuedRemovalsLeft);
}

bool SubscriptionStore::hasDeferredSubscriptionTreeNodesForPurging()
{
    std::shared_lock locker(subscriptions_lock);
    return !deferredSubscriptionLeafsForPurging.empty();
}

bool SubscriptionStore::purgeSubscriptionTree()
{
    bool deferredLeavesPresent = hasDeferredSubscriptionTreeNodesForPurging();

    if (deferredLeavesPresent)
    {
        std::unique_lock locker(subscriptions_lock);

        logger->log(LOG_INFO) << "Rebuilding subscription tree: we have " << deferredSubscriptionLeafsForPurging.size() << " deferred leafs to clean up. Doing some.";

        const std::chrono::time_point<std::chrono::steady_clock> limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);

        int counter = 0;
        for (; !deferredSubscriptionLeafsForPurging.empty(); deferredSubscriptionLeafsForPurging.pop_front())
        {
            if (limit < std::chrono::steady_clock::now())
                break;

            std::shared_ptr<SubscriptionNode> node = deferredSubscriptionLeafsForPurging.front().lock();

            if (node)
            {
                counter++;
                node->cleanSubscriptions(deferredSubscriptionLeafsForPurging, subscriptionDeferredCounter);
            }
        }

        logger->log(LOG_INFO) << "Rebuilding subscription tree: processed " << counter << " deferred leafs. Deferred leafs left: " << deferredSubscriptionLeafsForPurging.size();
    }
    else
    {
        std::unique_lock locker(subscriptions_lock);

        logger->logf(LOG_INFO, "Rebuilding subscription tree");
        subscriptionDeferredCounter = 0;
        root->cleanSubscriptions(deferredSubscriptionLeafsForPurging, subscriptionDeferredCounter);
        logger->log(LOG_INFO) << "Rebuilding subscription tree done, with " << deferredSubscriptionLeafsForPurging.size() << " deferred direct leafs to check";
    }

    const bool done = !hasDeferredSubscriptionTreeNodesForPurging();

    if (done)
        subscriptionCount = subscriptionDeferredCounter;

    return done;
}

bool SubscriptionStore::hasDeferredRetainedMessageNodesForPurging()
{
    RWLockGuard lock_guard(&retainedMessagesRwlock);
    lock_guard.rdlock();
    return !deferredRetainedMessageNodeToPurge.empty();
}

bool SubscriptionStore::expireRetainedMessages()
{
    bool deferredRetainedCleanup = hasDeferredRetainedMessageNodesForPurging();
    const std::chrono::time_point<std::chrono::steady_clock> limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);

    if (deferredRetainedCleanup)
    {
        RWLockGuard lock_guard(&retainedMessagesRwlock);
        lock_guard.wrlock();

        logger->log(LOG_INFO) << "Expiring retained messages: we have " << deferredRetainedMessageNodeToPurge.size() << " deferred leafs to expire. Doing some.";

        int counter = 0;
        for (; !deferredRetainedMessageNodeToPurge.empty(); deferredRetainedMessageNodeToPurge.pop_front())
        {
            if (limit < std::chrono::steady_clock::now())
                break;

            std::shared_ptr<RetainedMessageNode> node = deferredRetainedMessageNodeToPurge.front().lock();

            if (node)
            {
                counter++;
                this->expireRetainedMessages(node.get(), limit, deferredRetainedMessageNodeToPurge, retainedMessageDeferredCounter);
            }
        }

        logger->log(LOG_INFO) << "Expiring retained messages: processed " << counter << " deferred leafs. Deferred leafs left: " << deferredRetainedMessageNodeToPurge.size();
    }
    else
    {
        logger->log(LOG_INFO) << "Expiring retained messages.";

        RWLockGuard lock_guard(&retainedMessagesRwlock);
        lock_guard.wrlock();
        retainedMessageDeferredCounter = 0;
        this->expireRetainedMessages(retainedMessagesRoot.get(), limit, deferredRetainedMessageNodeToPurge, retainedMessageDeferredCounter);

        logger->log(LOG_INFO) << "Expiring retained messages done, with " << deferredRetainedMessageNodeToPurge.size() << " deferred nodes to check.";
    }

    const bool done = !hasDeferredRetainedMessageNodesForPurging();

    if (done)
        retainedMessageCount = retainedMessageDeferredCounter;

    return done;
}

/**
 * @brief SubscriptionStore::queueSessionRemoval places session efficiently in a sorted map that is periodically dequeued.
 * @param session
 */
void SubscriptionStore::queueSessionRemoval(const std::shared_ptr<Session> &session)
{
    if (!session)
        return;

    std::chrono::time_point<std::chrono::steady_clock> removeAt = std::chrono::steady_clock::now() + std::chrono::seconds(session->getSessionExpiryInterval());
    std::chrono::seconds secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(removeAt.time_since_epoch());
    session->setQueuedRemovalAt();

    std::lock_guard<std::mutex> locker(this->queuedSessionRemovalsMutex);
    queuedSessionRemovals[secondsSinceEpoch].push_back(session);
}

size_t SubscriptionStore::getRetainedMessageCount() const
{
    return retainedMessageCount;
}

uint64_t SubscriptionStore::getSessionCount() const
{
    return sessionsByIdConst.size();
}

size_t SubscriptionStore::getSubscriptionCount()
{
    return subscriptionCount;
}

void SubscriptionStore::getRetainedMessages(
    RetainedMessageNode *this_node, std::vector<RetainedMessage> &outputList,
    const std::chrono::time_point<std::chrono::steady_clock> &limit, const size_t limit_count,
    std::deque<std::weak_ptr<RetainedMessageNode>> &deferred) const
{
    {
        std::lock_guard<std::mutex> locker(this_node->messageSetMutex);
        if (this_node->message)
            outputList.push_back(*this_node->message);
    }

    for(auto &pair : this_node->children)
    {
        const std::shared_ptr<RetainedMessageNode> &child = pair.second;

        if (std::chrono::steady_clock::now() > limit || outputList.size() >= limit_count)
            deferred.push_back(child);
        else
            getRetainedMessages(child.get(), outputList, limit, limit_count, deferred);
    }
}

#ifdef TESTING
std::vector<RetainedMessage> SubscriptionStore::getAllRetainedMessages()
{
    RWLockGuard locker(&retainedMessagesRwlock);
    locker.rdlock();

    const std::shared_ptr<RetainedMessageNode> node = retainedMessagesRoot;
    std::vector<RetainedMessage> result;
    std::deque<std::weak_ptr<RetainedMessageNode>> deferred;
    auto time_limit = std::chrono::time_point<std::chrono::steady_clock>::max();
    getRetainedMessages(node.get(), result, time_limit, std::numeric_limits<size_t>::max(), deferred);
    return result;
}
#endif

/**
 * @brief SubscriptionStore::getSubscriptions
 * @param this_node
 * @param composedTopic
 * @param root bool. Every subtopic is concatenated with a '/', but not the first topic to 'root'. The root is a bit weird, virtual, so it needs different treatment.
 * @param outputList
 */
void SubscriptionStore::getSubscriptions(SubscriptionNode *this_node, const std::string &composedTopic, bool root,
                                         std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &outputList,
                                         std::deque<DeferredGetSubscription> &deferred,
                                         const std::chrono::time_point<std::chrono::steady_clock> limit) const
{
    // No code should make dummy nodes that are null, but still protecting against it.
    assert(this_node);
    if (!this_node)
        return;

    for (auto &pair : this_node->getSubscribers())
    {
        const Subscription &node = pair.second;
        std::shared_ptr<Session> ses = node.session.lock();
        if (ses)
        {
            SubscriptionForSerializing sub(ses->getClientId(), node.qos, node.noLocal, node.retainAsPublished, node.subscriptionIdentifier);
            outputList[composedTopic].push_back(sub);
        }
    }

    for (auto &pair : this_node->getSharedSubscribers())
    {
        const SharedSubscribers &node = pair.second;
        node.getForSerializing(composedTopic, outputList);
    }

    for (auto &pair : this_node->children)
    {
        SubscriptionNode *node = pair.second.get();
        const std::string topicAtNextLevel = root ? pair.first : composedTopic + "/" + pair.first;
        if (std::chrono::steady_clock::now() < limit)
            getSubscriptions(node, topicAtNextLevel, false, outputList, deferred, limit);
        else
            deferred.emplace_back(pair.second, topicAtNextLevel, false);
    }

    if (this_node->childrenPlus)
    {
        const std::string topicAtNextLevel = root ? "+" : composedTopic + "/+";
        if (std::chrono::steady_clock::now() < limit)
            getSubscriptions(this_node->childrenPlus.get(), topicAtNextLevel, false, outputList, deferred, limit);
        else
            deferred.emplace_back(this_node->childrenPlus, topicAtNextLevel, false);
    }

    if (this_node->childrenPound)
    {
        const std::string topicAtNextLevel = root ? "#" : composedTopic + "/#";
        if (std::chrono::steady_clock::now() < limit)
            getSubscriptions(this_node->childrenPound.get(), topicAtNextLevel, false, outputList, deferred, limit);
        else
            deferred.emplace_back(this_node->childrenPound, topicAtNextLevel, false);
    }
}

std::unordered_map<std::string, std::list<SubscriptionForSerializing>> SubscriptionStore::getSubscriptions()
{
    std::deque<DeferredGetSubscription> deferred;
    std::unordered_map<std::string, std::list<SubscriptionForSerializing>> subscriptionCopies;

    DeferredGetSubscription start(root, "", true);

    deferred.push_front(std::move(start));

    for (; !deferred.empty(); deferred.pop_front())
    {
        std::shared_lock locker(subscriptions_lock, std::defer_lock);

        if (Globals::getInstance().quitting)
            locker.lock();
        else
        {
            // TODO: C++ doesn't provide the non-portable pthread functions to avoid these try_lock read locks
            // from jumping the queue over waiting write locks. But, I'm not quite sure if I want reader
            // or writer starvation yet anyway...

            const auto try_lock_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
            while (std::chrono::steady_clock::now() < try_lock_timeout)
            {
                if (locker.try_lock())
                    break;

                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }

            if (!locker.owns_lock())
                locker.lock();
        }

        DeferredGetSubscription &def = deferred.front();
        std::shared_ptr<SubscriptionNode> node = def.node.lock();

        if (!node)
            continue;

        const std::chrono::time_point<std::chrono::steady_clock> limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
        getSubscriptions(node.get(), def.composedTopic, def.root, subscriptionCopies, deferred, limit);
    }

    return subscriptionCopies;
}

void SubscriptionStore::expireRetainedMessages(
    RetainedMessageNode *this_node, const std::chrono::time_point<std::chrono::steady_clock> &limit,
    std::deque<std::weak_ptr<RetainedMessageNode>> &deferred, size_t &real_message_counter)
{
    if (this_node->message && this_node->message->hasExpired())
    {
        this_node->message.reset();
    }

    if (this_node->message)
        real_message_counter++;

    auto cpos = this_node->children.begin();
    while (cpos != this_node->children.end())
    {
        auto cur = cpos;
        cpos++;

        if (std::chrono::steady_clock::now() > limit)
        {
            deferred.push_back(cur->second);
            continue;
        }

        const std::shared_ptr<RetainedMessageNode> &child = cur->second;
        expireRetainedMessages(child.get(), limit, deferred, real_message_counter);

        if (child->isOrphaned())
        {
            const Settings *settings = ThreadGlobals::getSettings();
            if (child->getMessageSetAt() + settings->retainedMessageNodeLifetime < std::chrono::steady_clock::now())
                this_node->children.erase(cur);
        }
    }
}

void SubscriptionStore::saveRetainedMessages(const std::string &filePath, bool in_background)
{
    logger->logf(LOG_NOTICE, "Saving retained messages to '%s'", filePath.c_str());

    std::deque<std::weak_ptr<RetainedMessageNode>> deferred;
    deferred.push_back(retainedMessagesRoot);

    RetainedMessagesDB db(filePath);
    db.openWrite();

    size_t total_count = 0;

    for (; !deferred.empty(); deferred.pop_front())
    {
        std::vector<RetainedMessage> result;

        {
            RWLockGuard locker(&retainedMessagesRwlock);
            locker.rdlock();

            std::shared_ptr<RetainedMessageNode> node = deferred.front().lock();

            if (!node)
                continue;

            const std::chrono::time_point<std::chrono::steady_clock> limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
            getRetainedMessages(node.get(), result, limit, 10000, deferred);
        }

        if (Globals::getInstance().quitting && in_background)
        {
            logger->log(LOG_NOTICE) << "Aborted background saving of retained messages because we're quitting. It will be reinitiated.";
            db.dontSaveTmpFile();
            return;
        }

        total_count += result.size();
        logger->log(LOG_DEBUG) << "Collected batch of " << result.size() << " retained messages to save.";
        db.saveData(result);

        // Because we only do this operation in background threads or on exit, we don't have to requeue, so can just sleep.
        if (in_background && !deferred.empty())
            std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    logger->log(LOG_NOTICE) << "Done saving " << total_count << " retained messages.";
}

void SubscriptionStore::loadRetainedMessages(const std::string &filePath)
{
    try
    {
        logger->logf(LOG_NOTICE, "Loading '%s'", filePath.c_str());

        RetainedMessagesDB db(filePath);
        db.openRead();

        size_t count = 0;
        size_t total_count = 0;
        do
        {
            std::list<RetainedMessage> messages = db.readData(1000);
            count = messages.size();
            total_count += count;

            for (RetainedMessage &rm : messages)
            {
                setRetainedMessage(rm.publish, rm.publish.getSubtopics());
            }
        } while (count > 0);

        logger->log(LOG_NOTICE) << "Done loading " << total_count << " retained messages.";
    }
    catch (PersistenceFileCantBeOpened &ex)
    {
        logger->logf(LOG_WARNING, "File '%s' is not there (yet)", filePath.c_str());
    }
}

void SubscriptionStore::saveSessionsAndSubscriptions(const std::string &filePath)
{
    logger->logf(LOG_NOTICE, "Saving sessions and subscriptions to '%s' in thread.", filePath.c_str());

    const std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();

    std::vector<std::shared_ptr<Session>> sessionPointers;
    std::unordered_map<std::string, std::list<SubscriptionForSerializing>> subscriptionCopies;

    {
        std::shared_lock session_locker(sessions_lock);

        sessionPointers.reserve(sessionsByIdConst.size());

        for (const auto &pair : sessionsByIdConst)
        {
            sessionPointers.push_back(pair.second);
        }
    }

    subscriptionCopies = getSubscriptions();

    const std::chrono::time_point<std::chrono::steady_clock> doneCopying = std::chrono::steady_clock::now();

    const std::chrono::milliseconds copyDuration = std::chrono::duration_cast<std::chrono::milliseconds>(doneCopying - start);
    logger->log(LOG_INFO) << "Collected " << sessionPointers.size() << " sessions and " << subscriptionCopies.size()
                           << " subscriptions to save, in " << copyDuration.count() << " ms (including defer time).";

    SessionsAndSubscriptionsDB db(filePath);
    db.openWrite();
    db.saveData(sessionPointers, subscriptionCopies);

    const std::chrono::time_point<std::chrono::steady_clock> doneSaving = std::chrono::steady_clock::now();

    const std::chrono::milliseconds saveDuration = std::chrono::duration_cast<std::chrono::milliseconds>(doneSaving - doneCopying);
    logger->log(LOG_INFO) << "Saved " << sessionPointers.size() << " sessions and " << subscriptionCopies.size()
                          << " subscriptions to '" << filePath << "', in " << saveDuration.count() << " ms.";
}

void SubscriptionStore::loadSessionsAndSubscriptions(const std::string &filePath)
{
    try
    {
        logger->logf(LOG_NOTICE, "Loading '%s'", filePath.c_str());

        SessionsAndSubscriptionsDB db(filePath);
        db.openRead();
        SessionsAndSubscriptionsResult loadedData = db.readData();

        std::unique_lock session_locker(sessions_lock);

        for (std::shared_ptr<Session> &session : loadedData.sessions)
        {
            sessionsById[session->getClientId()] = session;
            queueSessionRemoval(session);
            queueWillMessage(session->getWill(), session);
        }

        for (auto &pair : loadedData.subscriptions)
        {
            const std::string &topic = pair.first;
            const std::list<SubscriptionForSerializing> &subs = pair.second;

            for (const SubscriptionForSerializing &sub : subs)
            {
                std::shared_ptr<SubscriptionNode> subscriptionNode = getDeepestNode(splitTopic(topic));

                auto session_it = sessionsByIdConst.find(sub.clientId);
                if (session_it != sessionsByIdConst.end())
                {
                    const std::shared_ptr<Session> &ses = session_it->second;
                    subscriptionNode->addSubscriber(ses, sub.qos, sub.noLocal, sub.retainAsPublished, sub.shareName, sub.subscriptionidentifier);
                }

            }
        }
    }
    catch (PersistenceFileCantBeOpened &ex)
    {
        logger->logf(LOG_WARNING, "File '%s' is not there (yet)", filePath.c_str());
    }
}

ssize_t RetainedMessageNode::addPayload(const Publish &publish)
{
    std::lock_guard<std::mutex> locker(this->messageSetMutex);

    const bool retained_found = message.operator bool();
    ssize_t result = 0;

    if (retained_found)
        result--;

    if (publish.payload.empty())
    {
        if (retained_found)
            message.reset();
        return result;
    }

    RetainedMessage rm(publish);
    message = std::make_unique<RetainedMessage>(std::move(rm));
    result++;
    messageSetAt = std::chrono::steady_clock::now();
    return result;
}

/**
 * @brief RetainedMessageNode::getChildren return the children or nullptr when there are none. Const, so doesn't default construct.
 * @param subtopic
 * @return
 */
std::shared_ptr<RetainedMessageNode> RetainedMessageNode::getChildren(const std::string &subtopic) const
{
    auto it = children.find(subtopic);
    if (it != children.end())
        return it->second;
    return std::shared_ptr<RetainedMessageNode>();
}

bool RetainedMessageNode::isOrphaned() const
{
    return children.empty() && !message;
}

const std::chrono::time_point<std::chrono::steady_clock> RetainedMessageNode::getMessageSetAt() const
{
    return this->messageSetAt;
}

QueuedWill::QueuedWill(const std::shared_ptr<WillPublish> &will, const std::shared_ptr<Session> &session) :
    will(will),
    session(session)
{

}

const std::weak_ptr<WillPublish> &QueuedWill::getWill() const
{
    return this->will;
}

std::shared_ptr<Session> QueuedWill::getSession()
{
    return this->session.lock();
}


