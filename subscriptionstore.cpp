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
#include <deque>

ReceivingSubscriber::ReceivingSubscriber(const std::shared_ptr<Session> &ses, uint8_t qos, bool retainAsPublished) :
    session(ses),
    qos(qos),
    retainAsPublished(retainAsPublished)
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

void SubscriptionNode::addSubscriber(const std::shared_ptr<Session> &subscriber, uint8_t qos, bool noLocal, bool retainAsPublished, const std::string &shareName)
{
    if (!subscriber)
        return;

    Subscription sub;
    sub.session = subscriber;
    sub.qos = qos;
    sub.noLocal = noLocal;
    sub.retainAsPublished = retainAsPublished;

    const std::string &client_id = subscriber->getClientId();

    if (shareName.empty())
    {
        subscribers[client_id] = sub;
    }
    else
    {
        SharedSubscribers &subscribers = sharedSubscribers[shareName];
        subscribers.setName(shareName); // c++14 doesn't have try-emplace yet, in which case this separate step wouldn't be needed.
        subscribers[client_id] = sub;
    }
}

void SubscriptionNode::removeSubscriber(const std::shared_ptr<Session> &subscriber, const std::string &shareName)
{
    Subscription sub;
    sub.session = subscriber;
    sub.qos = 0;

    const std::string &clientId = subscriber->getClientId();

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

/**
 * @brief SubscriptionNode::getChildren gets children or null pointer. Const, so doesn't default-create node for
 *        non-existing children.
 * @param subtopic
 * @return
 */
SubscriptionNode *SubscriptionNode::getChildren(const std::string &subtopic) const
{
    auto it = children.find(subtopic);
    if (it != children.end())
        return it->second.get();
    return nullptr;
}


SubscriptionStore::SubscriptionStore() :
    root(),
    rootDollar(),
    sessionsByIdConst(sessionsById)
{

}

/**
 * @brief SubscriptionStore::getDeepestNode gets the node in the tree walking the path of 'the/subscription/topic/path', making new nodes as required.
 * @param topic
 * @param subtopics
 * @return
 *
 * caller is responsible for locking.
 */
SubscriptionNode *SubscriptionStore::getDeepestNode(const std::vector<std::string> &subtopics)
{
    SubscriptionNode *deepestNode = &root;
    if (!subtopics.empty())
    {
        const std::string &first = subtopics.front();
        if (first.length() > 0 && first[0] == '$')
            deepestNode = &rootDollar;
    }

    for(const std::string &subtopic : subtopics)
    {
        std::shared_ptr<SubscriptionNode> *selectedChildren = nullptr;

        if (subtopic == "#")
            selectedChildren = &deepestNode->childrenPound;
        else if (subtopic == "+")
            selectedChildren = &deepestNode->childrenPlus;
        else
            selectedChildren = &deepestNode->children[subtopic];

        std::shared_ptr<SubscriptionNode> &node = *selectedChildren;

        if (!node)
        {
            node = std::make_shared<SubscriptionNode>();
        }
        deepestNode = node.get();
    }

    assert(deepestNode);
    return deepestNode;
}

void SubscriptionStore::addSubscription(std::shared_ptr<Client> &client, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished)
{
    const static std::string empty;
    addSubscription(client, subtopics, qos, noLocal, retainAsPublished, empty, AuthResult::success);
}

void SubscriptionStore::addSubscription(std::shared_ptr<Client> &client, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished,
                                        const std::string &shareName, AuthResult authResult)
{
    RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
    lock_guard.wrlock();

    SubscriptionNode *deepestNode = getDeepestNode(subtopics);

    if (!deepestNode)
        return;

    auto session_it = sessionsByIdConst.find(client->getClientId());
    if (session_it == sessionsByIdConst.end())
        return;

    const std::shared_ptr<Session> ses = session_it->second;

    if (!ses)
        return;

    deepestNode->addSubscriber(ses, qos, noLocal, retainAsPublished, shareName);
    subscriptionCount++;
    lock_guard.unlock();

    if (authResult == AuthResult::success && shareName.empty())
        giveClientRetainedMessages(ses, subtopics, qos);

}

void SubscriptionStore::removeSubscription(std::shared_ptr<Client> &client, const std::string &topic)
{
    std::vector<std::string> subtopics = splitTopic(topic);

    std::string shareName;
    parseSubscriptionShare(subtopics, shareName);

    SubscriptionNode *deepestNode = &root;
    if (!subtopics.empty())
    {
        const std::string &first = subtopics.front();
        if (first.length() > 0 && first[0] == '$')
            deepestNode = &rootDollar;
    }

    RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
    lock_guard.wrlock();

    // This code looks like that for addSubscription(), but it's specifically different in that we don't want to default-create non-existing
    // nodes. We need to abort when that happens.
    for(const std::string &subtopic : subtopics)
    {
        SubscriptionNode *selectedChildren = nullptr;

        if (subtopic == "#")
            selectedChildren = deepestNode->childrenPound.get();
        else if (subtopic == "+")
            selectedChildren = deepestNode->childrenPlus.get();
        else
            selectedChildren = deepestNode->getChildren(subtopic);

        if (!selectedChildren)
        {
            return;
        }
        deepestNode = selectedChildren;
    }

    assert(deepestNode);

    if (deepestNode)
    {
        auto session_it = sessionsByIdConst.find(client->getClientId());
        if (session_it != sessionsByIdConst.end())
        {
            const std::shared_ptr<Session> &ses = session_it->second;
            deepestNode->removeSubscriber(ses, shareName);
            subscriptionCount--;
        }
    }

    lock_guard.unlock();


}

std::shared_ptr<Session> SubscriptionStore::getBridgeSession(std::shared_ptr<Client> &client)
{
    RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
    lock_guard.wrlock();

    std::shared_ptr<Session> session = std::make_shared<Session>();
    session->assignActiveConnection(client);
    client->assignSession(session);
    sessionsById[client->getClientId()] = session;
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
    ThreadGlobals::getThreadData()->queueClientNextKeepAliveCheckLocked(client, true);

    // These destructors need to be called outside the sessions lock, so placing here.
    std::shared_ptr<Session> session;
    std::shared_ptr<Client> clientOfOtherSession;

    if (client->getClientId().empty())
        throw ProtocolError("Trying to store client without an ID.", ReasonCodes::ProtocolError);

    {
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.wrlock();

        auto session_it = sessionsById.find(client->getClientId());
        if (session_it != sessionsById.end())
        {
            session = session_it->second;

            if (session)
            {
                clientOfOtherSession = session->makeSharedClient();

                if (clientOfOtherSession)
                {
                    logger->logf(LOG_NOTICE, "Disconnecting existing client with id '%s'", clientOfOtherSession->getClientId().c_str());
                    clientOfOtherSession->setDisconnectReason("Another client with this ID connected");
                    clientOfOtherSession->serverInitiatedDisconnect(ReasonCodes::SessionTakenOver);
                }

            }
        }

        if (!session || session->getDestroyOnDisconnect() || clean_start)
        {
            // Don't use sdt::make_shared to avoid the weak pointers from retaining the size of session in the control block.
            session = std::shared_ptr<Session>(new Session());

            sessionsById[client->getClientId()] = session;
        }
    }

    session->assignActiveConnection(client);
    client->assignSession(session);
    session->setSessionProperties(clientReceiveMax, sessionExpiryInterval, clean_start, client->getProtocolVersion());
    session->sendAllPendingQosData();
}

/**
 * @brief SubscriptionStore::lockSession returns the session if it exists. Returning is done keep the shared pointer active, to
 * avoid race conditions with session removal.
 * @param clientid
 * @return
 */
std::shared_ptr<Session> SubscriptionStore::lockSession(const std::string &clientid)
{
    RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
    lock_guard.rdlock();

    auto it = sessionsByIdConst.find(clientid);
    if (it != sessionsByIdConst.end())
    {
        return it->second;
    }
    return std::shared_ptr<Session>();
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
    Authentication &auth = *ThreadGlobals::getAuth();

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

            // If sessions get a new will, or the will is cleared from a new connecting client, this entry
            // will be null and we can ignore it.
            if (p)
            {
                std::shared_ptr<Session> s = will.getSession();

                // Check for stale wills, or sessions that have become active again.
                if (s && !s->hasActiveClient())
                {
                    logger->logf(LOG_DEBUG, "Sending delayed will on topic '%s'.", p->topic.c_str() );
                    if (auth.aclCheck(*p, p->payload) == AuthResult::success)
                    {
                        PublishCopyFactory factory(p.get());
                        queuePacketAtSubscribers(factory, p->client_id);

                        if (p->retain)
                            setRetainedMessage(*p, p->getSubtopics());
                    }

                    s->clearWill();
                }
            }
        }
        it = pendingWillMessages.erase(it);
    }
}

/**
 * @brief SubscriptionStore::queueWillMessage queues the will message in a sorted map.
 * @param willMessage
 * @param forceNow
 *
 * The queued will is only valid for that time. Should a new will be placed in the map for a session, the original shared_ptr
 * will be cleared and the previously queued entry is void (but still there, so it needs to be checked).
 */
void SubscriptionStore::queueWillMessage(const std::shared_ptr<WillPublish> &willMessage, const std::string &senderClientId, const std::shared_ptr<Session> &session, bool forceNow)
{
    if (!willMessage)
        return;

    Authentication &auth = *ThreadGlobals::getAuth();
    Settings *settings = ThreadGlobals::getSettings();

    const int delay = forceNow ? 0 : willMessage->will_delay;
    logger->logf(LOG_DEBUG, "Queueing will on topic '%s', with delay %d seconds.", willMessage->topic.c_str(), delay );

    if (delay == 0)
    {
        if (settings->willsEnabled && auth.aclCheck(*willMessage, willMessage->payload) == AuthResult::success)
        {
            PublishCopyFactory factory(willMessage.get());
            queuePacketAtSubscribers(factory, senderClientId);

            if (willMessage->retain)
                setRetainedMessage(*willMessage.get(), (*willMessage).getSubtopics());
        }

        // Avoid sending two immediate wills when a session is destroyed with the client disconnect.
        // Session is null when you're destroying a client before a session is assigned, or
        // when an old client has no session anymore after a client with the same ID connects.
        if (session)
            session->clearWill();

        return;
    }

    willMessage->setQueuedAt();

    QueuedWill queuedWill(willMessage, session);
    const std::chrono::time_point<std::chrono::steady_clock> sendWillAt = std::chrono::steady_clock::now() + std::chrono::seconds(willMessage->will_delay);
    std::chrono::seconds secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(sendWillAt.time_since_epoch());

    std::lock_guard<std::mutex> locker(this->pendingWillsMutex);
    this->pendingWillMessages[secondsSinceEpoch].push_back(queuedWill);
}

void SubscriptionStore::publishNonRecursively(SubscriptionNode *this_node, std::forward_list<ReceivingSubscriber> &targetSessions, size_t distributionHash,
                                              const std::string &senderClientId)
{
    {
        const std::unordered_map<std::string, Subscription> &subscribers = this_node->getSubscribers();

        for (auto &pair : subscribers)
        {
            const Subscription &sub = pair.second;

            const std::shared_ptr<Session> session = sub.session.lock();
            if (session) // Shared pointer expires when session has been cleaned by 'clean session' connect.
            {
                const std::string &receiverClientId = session->getClientId();

                if (sub.noLocal && receiverClientId == senderClientId)
                    continue;

                targetSessions.emplace_front(session, sub.qos, sub.retainAsPublished);
            }
        }
    }

    {
        std::unordered_map<std::string, SharedSubscribers> &sharedSubscribers = this_node->getSharedSubscribers();

        if (!sharedSubscribers.empty())
        {
            const Settings *settings = ThreadGlobals::getSettings();

            for(auto &pair : sharedSubscribers)
            {
                SharedSubscribers &subscribers = pair.second;

                const Subscription *sub = nullptr;

                if (settings->sharedSubscriptionTargeting == SharedSubscriptionTargeting::SenderHash)
                    sub = subscribers.getNext(distributionHash);
                else
                    sub = subscribers.getNext();

                if (sub == nullptr)
                    continue;

                const std::shared_ptr<Session> session = sub->session.lock();
                if (session) // Shared pointer expires when session has been cleaned by 'clean session' connect.
                {
                    targetSessions.emplace_front(session, sub->qos, sub->retainAsPublished);
                }
            }
        }
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
void SubscriptionStore::publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                                           SubscriptionNode *this_node, std::forward_list<ReceivingSubscriber> &targetSessions, size_t distributionHash,
                                           const std::string &senderClientId)
{
    if (cur_subtopic_it == end) // This is the end of the topic path, so look for subscribers here.
    {
        if (this_node)
        {
            publishNonRecursively(this_node, targetSessions, distributionHash, senderClientId);

            // Subscribing to 'one/two/three/#' also gives you 'one/two/three'.
            if (this_node->childrenPound)
            {
                publishNonRecursively(this_node->childrenPound.get(), targetSessions, distributionHash, senderClientId);
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
        publishNonRecursively(this_node->childrenPound.get(), targetSessions, distributionHash, senderClientId);
    }

    const auto &sub_node = this_node->children.find(cur_subtop);

    if (this_node->childrenPlus)
    {
        publishRecursively(next_subtopic, end, this_node->childrenPlus.get(), targetSessions, distributionHash, senderClientId);
    }

    if (sub_node != this_node->children.end())
    {
        publishRecursively(next_subtopic, end, sub_node->second.get(), targetSessions, distributionHash, senderClientId);
    }
}

void SubscriptionStore::queuePacketAtSubscribers(PublishCopyFactory &copyFactory, const std::string &senderClientId, bool dollar)
{
    /*
     * Sometimes people publish or set as will topics with dollar. Node-to-Node communication for bridges for instance.
     * We still accept them, but just decide to do nothing with them. Only the FlashMQ internals are allowed to publish
     * on dollar topics.
     */
    if (!dollar)
    {
        const std::string &topic = copyFactory.getTopic();

        if (!topic.empty() && topic[0] == '$')
        {
            return;
        }
    }

    SubscriptionNode *startNode = dollar ? &rootDollar : &root;

    std::forward_list<ReceivingSubscriber> subscriberSessions;

    {
        const std::vector<std::string> &subtopics = copyFactory.getSubtopics();
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.rdlock();
        publishRecursively(subtopics.begin(), subtopics.end(), startNode, subscriberSessions, copyFactory.getSharedSubscriptionHashKey(), senderClientId);
    }

    for(const ReceivingSubscriber &x : subscriberSessions)
    {
        x.session->writePacket(copyFactory, x.qos, x.retainAsPublished);
    }
}

void SubscriptionStore::giveClientRetainedMessagesRecursively(std::vector<std::string>::const_iterator cur_subtopic_it,
                                                              std::vector<std::string>::const_iterator end,
                                                              const std::shared_ptr<RetainedMessageNode> &this_node,
                                                              bool poundMode,
                                                              const std::shared_ptr<Session> &session, const uint8_t max_qos,
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
                    const PacketDropReason drop_reason = session->writePacket(copyFactory, max_qos, true);

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
                    giveClientRetainedMessagesRecursively(cur_subtopic_it, end, child, poundMode, session, max_qos, limit, deferred, drop_count, ++processed_nodes_count);
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
        giveClientRetainedMessagesRecursively(next_subtopic, end, this_node, true, session, max_qos, limit, deferred, drop_count, ++processed_nodes_count);
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
                giveClientRetainedMessagesRecursively(next_subtopic, end, child, false, session, max_qos, limit, deferred, drop_count, ++processed_nodes_count);
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
                giveClientRetainedMessagesRecursively(next_subtopic, end, children, false, session, max_qos, limit, deferred, drop_count, ++processed_nodes_count);
            }
        }
    }
}

void SubscriptionStore::giveClientRetainedMessagesInitiateDeferred(const std::weak_ptr<Session> ses,
                                                                   const std::shared_ptr<const std::vector<std::string>> subscribeSubtopicsCopy,
                                                                   std::shared_ptr<std::deque<DeferredRetainedMessageNodeDelivery>> deferred,
                                                                   int &requeue_count, uint &total_node_count, uint8_t max_qos)
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
        giveClientRetainedMessagesRecursively(d.cur, d.end, node, d.poundMode, session, max_qos, new_limit, *deferred, drop_count, processed_nodes);
    }

    total_node_count += processed_nodes;

    if (processed_nodes > 0)
    {
        requeue_count = 0;
    }

    if (!deferred->empty() && ++requeue_count < 100 && total_node_count < settings->retainedMessagesNodeLimit)
    {
        ThreadData *t = ThreadGlobals::getThreadData();
        auto again = std::bind(&SubscriptionStore::giveClientRetainedMessagesInitiateDeferred, this, ses, subscribeSubtopicsCopy, deferred, requeue_count, total_node_count, max_qos);

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
                                                   const std::vector<std::string> &subscribeSubtopics, uint8_t max_qos)
{
    if (!ses)
        return;

    const Settings *settings = ThreadGlobals::getSettings();

    if (settings->retainedMessagesMode != RetainedMessagesMode::Enabled)
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
    giveClientRetainedMessagesInitiateDeferred(ses, subscribeSubtopicsCopy, deferred, requeue_count, total_node_count, max_qos);
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

    if (settings->retainedMessagesMode != RetainedMessagesMode::Enabled)
        return true;

    RetainedMessageNode *deepestNode = retainedMessagesRoot.get();
    if (!subtopics.empty() && !subtopics[0].empty() > 0 && subtopics[0][0] == '$')
        deepestNode = retainedMessagesRootDollar.get();

    bool needsWriteLock = false;
    auto subtopic_pos = subtopics.begin();

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
            auto pos = deepestNode->children.find(*subtopic_pos);

            if (pos == deepestNode->children.end())
            {
                needsWriteLock = true;
                break;
            }

            std::shared_ptr<RetainedMessageNode> &selectedChildren = pos->second;

            if (!selectedChildren)
            {
                needsWriteLock = true;
                break;
            }
            deepestNode = selectedChildren.get();
            subtopic_pos++;
        }

        assert(deepestNode);

        if (!needsWriteLock && deepestNode)
        {
            deepestNode->addPayload(publish, retainedMessageCount);
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

        while(subtopic_pos != subtopics.end())
        {
            std::shared_ptr<RetainedMessageNode> &selectedChildren = deepestNode->children[*subtopic_pos];

            if (!selectedChildren)
            {
                selectedChildren = std::make_shared<RetainedMessageNode>();
            }
            deepestNode = selectedChildren.get();
            subtopic_pos++;
        }

        assert(deepestNode);

        if (deepestNode)
        {
            deepestNode->addPayload(publish, retainedMessageCount);
        }
    }

    return true;
}

// Clean up the weak pointers to sessions and remove nodes that are empty.
int SubscriptionNode::cleanSubscriptions(std::deque<std::weak_ptr<SubscriptionNode>> &defferedLeafs)
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

        int n = node->cleanSubscriptions(defferedLeafs);
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
        int n = node_->cleanSubscriptions(defferedLeafs);
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

    return subscribers.size() + sharedSubscribers.size() + subscribersLeftInChildren;
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
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.wrlock();

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

        std::shared_ptr<WillPublish> &will = s->getWill();
        if (will)
        {
            queueWillMessage(will, clientid, s, true);
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
    RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
    lock_guard.rdlock();
    return !deferredSubscriptionLeafsForPurging.empty();
}

bool SubscriptionStore::purgeSubscriptionTree()
{
    bool deferredLeavesPresent = hasDeferredSubscriptionTreeNodesForPurging();

    if (deferredLeavesPresent)
    {
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.wrlock();

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
                node->cleanSubscriptions(deferredSubscriptionLeafsForPurging);
            }
        }

        logger->log(LOG_INFO) << "Rebuilding subscription tree: processed " << counter << " deferred leafs. Deferred leafs left: " << deferredSubscriptionLeafsForPurging.size();
    }
    else
    {
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.wrlock();

        logger->logf(LOG_INFO, "Rebuilding subscription tree");
        root.cleanSubscriptions(deferredSubscriptionLeafsForPurging);
        logger->log(LOG_INFO) << "Rebuilding subscription tree done, with " << deferredSubscriptionLeafsForPurging.size() << " deferred direct leafs to check";
    }

    return deferredSubscriptionLeafsForPurging.empty();
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
                this->expireRetainedMessages(node.get(), limit, deferredRetainedMessageNodeToPurge);
            }
        }

        logger->log(LOG_INFO) << "Expiring retained messages: processed " << counter << " deferred leafs. Deferred leafs left: " << deferredRetainedMessageNodeToPurge.size();
    }
    else
    {
        logger->log(LOG_INFO) << "Expiring retained messages.";

        RWLockGuard lock_guard(&retainedMessagesRwlock);
        lock_guard.wrlock();
        this->expireRetainedMessages(retainedMessagesRoot.get(), limit, deferredRetainedMessageNodeToPurge);

        logger->log(LOG_INFO) << "Expiring retained messages done, with " << deferredRetainedMessageNodeToPurge.size() << " deferred nodes to check.";
    }

    return deferredRetainedMessageNodeToPurge.empty();
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

int64_t SubscriptionStore::getRetainedMessageCount() const
{
    return retainedMessageCount;
}

uint64_t SubscriptionStore::getSessionCount() const
{
    return sessionsByIdConst.size();
}

/**
 * @brief SubscriptionStore::getSubscriptionCount Gets the approximate subscription count and occassionally accurately refreshes.
 * @return
 *
 * With sessions that expire, log off, etc, one would have to validate each subscription's existence to count accurately. It's just not worth
 * it to do so.
 */
int64_t SubscriptionStore::getSubscriptionCount()
{
    return 0;

    /*
    if (this->lastSubscriptionCountRefreshedAt + std::chrono::minutes(30) > std::chrono::steady_clock::now())
        return this->subscriptionCount;

    int64_t count = 0;

    {
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.rdlock();

        countSubscriptions(&root, count);
        countSubscriptions(&rootDollar, count);
    }

    {
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.wrlock();

        lastSubscriptionCountRefreshedAt = std::chrono::steady_clock::now();
        this->subscriptionCount = count;
    }

    return count;
    */
}

void SubscriptionStore::getRetainedMessages(RetainedMessageNode *this_node, std::vector<RetainedMessage> &outputList,
                                            const std::chrono::time_point<std::chrono::steady_clock> &limit,
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

        if (std::chrono::steady_clock::now() > limit)
            deferred.push_back(child);
        else
            getRetainedMessages(child.get(), outputList, limit, deferred);
    }
}

/**
 * @brief SubscriptionStore::getSubscriptions
 * @param this_node
 * @param composedTopic
 * @param root bool. Every subtopic is concatenated with a '/', but not the first topic to 'root'. The root is a bit weird, virtual, so it needs different treatment.
 * @param outputList
 */
void SubscriptionStore::getSubscriptions(SubscriptionNode *this_node, const std::string &composedTopic, bool root,
                                         std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &outputList) const
{
    for (auto &pair : this_node->getSubscribers())
    {
        const Subscription &node = pair.second;
        std::shared_ptr<Session> ses = node.session.lock();
        if (ses)
        {
            SubscriptionForSerializing sub(ses->getClientId(), node.qos, node.noLocal, node.retainAsPublished);
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
        getSubscriptions(node, topicAtNextLevel, false, outputList);
    }

    if (this_node->childrenPlus)
    {
        const std::string topicAtNextLevel = root ? "+" : composedTopic + "/+";
        getSubscriptions(this_node->childrenPlus.get(), topicAtNextLevel, false, outputList);
    }

    if (this_node->childrenPound)
    {
        const std::string topicAtNextLevel = root ? "#" : composedTopic + "/#";
        getSubscriptions(this_node->childrenPound.get(), topicAtNextLevel, false, outputList);
    }
}

void SubscriptionStore::countSubscriptions(SubscriptionNode *this_node, int64_t &count) const
{
    if (!this_node)
        return;

    for (auto &pair : this_node->getSubscribers())
    {
        const Subscription &node = pair.second;
        std::shared_ptr<Session> ses = node.session.lock();
        if (ses)
        {
            count++;
        }
    }

    for (auto &pair : this_node->children)
    {
        SubscriptionNode *node = pair.second.get();
        countSubscriptions(node, count);
    }

    if (this_node->childrenPlus)
    {
        countSubscriptions(this_node->childrenPlus.get(), count);
    }

    if (this_node->childrenPound)
    {
        countSubscriptions(this_node->childrenPound.get(), count);
    }
}

void SubscriptionStore::expireRetainedMessages(RetainedMessageNode *this_node, const std::chrono::time_point<std::chrono::steady_clock> &limit,
                                               std::deque<std::weak_ptr<RetainedMessageNode>> &deferred)
{
    if (this_node->message && this_node->message->hasExpired())
    {
        this_node->message.reset();
        this->retainedMessageCount--;
    }

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
        expireRetainedMessages(child.get(), limit, deferred);

        if (child->isOrphaned())
        {
            const Settings *settings = ThreadGlobals::getSettings();
            if (child->getMessageSetAt() + settings->retainedMessageNodeLifetime < std::chrono::steady_clock::now())
                this_node->children.erase(cur);
        }
    }
}

void SubscriptionStore::saveRetainedMessages(const std::string &filePath, bool sleep_after_limit)
{
    logger->logf(LOG_NOTICE, "Saving retained messages to '%s'", filePath.c_str());

    std::vector<RetainedMessage> result;
    int64_t reserve = std::max<int64_t>(retainedMessageCount, 0);
    reserve = std::min<int64_t>(reserve, 1000000);
    result.reserve(reserve);

    std::deque<std::weak_ptr<RetainedMessageNode>> deferred;

    deferred.push_back(retainedMessagesRoot);

    for (; !deferred.empty(); deferred.pop_front())
    {
        {
            RWLockGuard locker(&retainedMessagesRwlock);
            locker.rdlock();

            std::shared_ptr<RetainedMessageNode> node = deferred.front().lock();

            if (!node)
                continue;

            const std::chrono::time_point<std::chrono::steady_clock> limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
            getRetainedMessages(node.get(), result, limit, deferred);
        }

        // Because we only do this operation in background threads or on exit, we don't have to requeue, so can just sleep.
        if (sleep_after_limit && !deferred.empty())
            std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    logger->log(LOG_DEBUG) << "Collected " << result.size() << " retained messages to save.";

    // Then do the IO without locking the threads.
    RetainedMessagesDB db(filePath);
    db.openWrite();
    db.saveData(result);
}

void SubscriptionStore::loadRetainedMessages(const std::string &filePath)
{
    try
    {
        logger->logf(LOG_NOTICE, "Loading '%s'", filePath.c_str());

        RetainedMessagesDB db(filePath);
        db.openRead();
        std::list<RetainedMessage> messages = db.readData();

        for (RetainedMessage &rm : messages)
        {
            setRetainedMessage(rm.publish, rm.publish.getSubtopics());
        }
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
        RWLockGuard lock_guard(&sessionsAndSubscriptionsRwlock);
        lock_guard.rdlock();

        sessionPointers.reserve(sessionsByIdConst.size());

        for (const auto &pair : sessionsByIdConst)
        {
            sessionPointers.push_back(pair.second);
        }

        getSubscriptions(&root, "", true, subscriptionCopies);
    }

    const std::chrono::time_point<std::chrono::steady_clock> doneCopying = std::chrono::steady_clock::now();

    const std::chrono::milliseconds copyDuration = std::chrono::duration_cast<std::chrono::milliseconds>(doneCopying - start);
    logger->log(LOG_DEBUG) << "Collected " << sessionPointers.size() << " sessions and " << subscriptionCopies.size()
                           << " subscriptions to save, in " << copyDuration.count() << " ms.";

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

        RWLockGuard locker(&sessionsAndSubscriptionsRwlock);
        locker.wrlock();

        for (std::shared_ptr<Session> &session : loadedData.sessions)
        {
            sessionsById[session->getClientId()] = session;
            queueSessionRemoval(session);
            queueWillMessage(session->getWill(), session->getClientId(), session);
        }

        for (auto &pair : loadedData.subscriptions)
        {
            const std::string &topic = pair.first;
            const std::list<SubscriptionForSerializing> &subs = pair.second;

            for (const SubscriptionForSerializing &sub : subs)
            {
                SubscriptionNode *subscriptionNode = getDeepestNode(splitTopic(topic));

                auto session_it = sessionsByIdConst.find(sub.clientId);
                if (session_it != sessionsByIdConst.end())
                {
                    const std::shared_ptr<Session> &ses = session_it->second;
                    subscriptionNode->addSubscriber(ses, sub.qos, sub.noLocal, sub.retainAsPublished, sub.shareName);
                }

            }
        }
    }
    catch (PersistenceFileCantBeOpened &ex)
    {
        logger->logf(LOG_WARNING, "File '%s' is not there (yet)", filePath.c_str());
    }
}

void RetainedMessageNode::addPayload(const Publish &publish, int64_t &totalCount)
{
    std::lock_guard<std::mutex> locker(this->messageSetMutex);

    const bool retained_found = message.operator bool();

    if (retained_found)
        totalCount--;

    if (publish.payload.empty())
    {
        if (retained_found)
            message.reset();
        return;
    }

    RetainedMessage rm(publish);
    message = std::make_unique<RetainedMessage>(std::move(rm));
    totalCount += 1;
    messageSetAt = std::chrono::steady_clock::now();
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


