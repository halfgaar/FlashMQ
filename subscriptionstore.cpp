/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#include "subscriptionstore.h"

#include "cassert"

#include "rwlockguard.h"


SubscriptionNode::SubscriptionNode(const std::string &subtopic) :
    subtopic(subtopic)
{

}

std::vector<Subscription> &SubscriptionNode::getSubscribers()
{
    return subscribers;
}

void SubscriptionNode::addSubscriber(const std::shared_ptr<Session> &subscriber, char qos)
{
    Subscription sub;
    sub.session = subscriber;
    sub.qos = qos;

    // I'll have to decide whether to keep the subscriber as a vector. Vectors are
    // fast, and relatively, you don't often add subscribers.
    if (std::find(subscribers.begin(), subscribers.end(), sub) == subscribers.end())
    {
        subscribers.push_back(sub);
    }
}

void SubscriptionNode::removeSubscriber(const std::shared_ptr<Session> &subscriber)
{
    Subscription sub;
    sub.session = subscriber;
    sub.qos = 0;

    auto it = std::find(subscribers.begin(), subscribers.end(), sub);

    if (it != subscribers.end())
    {
        subscribers.erase(it);
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
    root("root"),
    rootDollar("rootDollar"),
    sessionsByIdConst(sessionsById)
{

}

void SubscriptionStore::addSubscription(std::shared_ptr<Client> &client, const std::string &topic, const std::vector<std::string> &subtopics, char qos)
{
    SubscriptionNode *deepestNode = &root;
    if (topic.length() > 0 && topic[0] == '$')
        deepestNode = &rootDollar;

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    for(const std::string &subtopic : subtopics)
    {
        std::unique_ptr<SubscriptionNode> *selectedChildren = nullptr;

        if (subtopic == "#")
            selectedChildren = &deepestNode->childrenPound;
        else if (subtopic == "+")
            selectedChildren = &deepestNode->childrenPlus;
        else
            selectedChildren = &deepestNode->children[subtopic];

        std::unique_ptr<SubscriptionNode> &node = *selectedChildren;

        if (!node)
        {
            node.reset(new SubscriptionNode(subtopic));
        }
        deepestNode = node.get();
    }

    assert(deepestNode);

    if (deepestNode)
    {
        auto session_it = sessionsByIdConst.find(client->getClientId());
        if (session_it != sessionsByIdConst.end())
        {
            const std::shared_ptr<Session> &ses = session_it->second;
            deepestNode->addSubscriber(ses, qos);
            uint64_t count = giveClientRetainedMessages(ses, subtopics, qos);
            client->getThreadData()->incrementSentMessageCount(count);
        }
    }

    lock_guard.unlock();
}

void SubscriptionStore::removeSubscription(std::shared_ptr<Client> &client, const std::string &topic)
{
    const std::list<std::string> subtopics = split(topic, '/');

    SubscriptionNode *deepestNode = &root;
    if (topic.length() > 0 && topic[0] == '$')
        deepestNode = &rootDollar;

    RWLockGuard lock_guard(&subscriptionsRwlock);
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
            deepestNode->removeSubscriber(ses);
        }
    }

    lock_guard.unlock();


}

// Removes an existing client when it already exists [MQTT-3.1.4-2].
void SubscriptionStore::registerClientAndKickExistingOne(std::shared_ptr<Client> &client)
{
    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    if (client->getClientId().empty())
        throw ProtocolError("Trying to store client without an ID.");

    std::shared_ptr<Session> session;
    auto session_it = sessionsById.find(client->getClientId());
    if (session_it != sessionsById.end())
    {
        session = session_it->second;

        if (session && !session->clientDisconnected())
        {
            std::shared_ptr<Client> cl = session->makeSharedClient();
            logger->logf(LOG_NOTICE, "Disconnecting existing client with id '%s'", cl->getClientId().c_str());
            cl->setReadyForDisconnect();
            cl->getThreadData()->removeClient(cl);
            cl->markAsDisconnecting();
        }
    }

    if (!session || client->getCleanSession())
    {
        session.reset(new Session());

        sessionsById[client->getClientId()] = session;
    }

    session->assignActiveConnection(client);
    client->assignSession(session);
    uint64_t count = session->sendPendingQosMessages();
    client->getThreadData()->incrementSentMessageCount(count);
}

bool SubscriptionStore::sessionPresent(const std::string &clientid)
{
    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.rdlock();

    bool result = false;

    auto it = sessionsByIdConst.find(clientid);
    if (it != sessionsByIdConst.end())
    {
        it->second->touch(); // Touching to avoid a race condition between using the session after this, and it expiring.
        result = true;
    }
    return result;
}

void SubscriptionStore::publishNonRecursively(const MqttPacket &packet, const std::vector<Subscription> &subscribers, uint64_t &count) const
{
    for (const Subscription &sub : subscribers)
    {
        std::weak_ptr<Session> session_weak = sub.session;
        if (!session_weak.expired()) // Shared pointer expires when session has been cleaned by 'clean session' connect.
        {
            const std::shared_ptr<Session> session = session_weak.lock();
            session->writePacket(packet, sub.qos, false, count);
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
 * As noted in the params section, this method was written so that it could be (somewhat) optimized for tail recursion by the kernel. If you refactor this,
 * look at objdump --disassemble --demangle to see how many calls (not jumps) to itself are made and compare.
 */
void SubscriptionStore::publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                                           SubscriptionNode *this_node, const MqttPacket &packet, uint64_t &count) const
{
    if (cur_subtopic_it == end) // This is the end of the topic path, so look for subscribers here.
    {
        if (this_node)
            publishNonRecursively(packet, this_node->getSubscribers(), count);
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
        publishNonRecursively(packet, this_node->childrenPound->getSubscribers(), count);
    }

    const auto &sub_node = this_node->children.find(cur_subtop);
    if (sub_node != this_node->children.end())
    {
        publishRecursively(next_subtopic, end, sub_node->second.get(), packet, count);
    }

    if (this_node->childrenPlus)
    {
        publishRecursively(next_subtopic, end, this_node->childrenPlus.get(), packet, count);
    }
}

void SubscriptionStore::queuePacketAtSubscribers(const std::vector<std::string> &subtopics, const MqttPacket &packet, bool dollar)
{
    assert(subtopics.size() > 0);

    SubscriptionNode *startNode = dollar ? &rootDollar : &root;

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.rdlock();

    uint64_t count = 0;
    publishRecursively(subtopics.begin(), subtopics.end(), startNode, packet, count);

    std::shared_ptr<Client> sender = packet.getSender();
    if (sender)
    {
        sender->getThreadData()->incrementSentMessageCount(count);
    }
}

void SubscriptionStore::giveClientRetainedMessagesRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                                                              RetainedMessageNode *this_node, char max_qos, const std::shared_ptr<Session> &ses,
                                                              bool poundMode, uint64_t &count) const
{
    if (cur_subtopic_it == end)
    {
        for(const RetainedMessage &rm : this_node->retainedMessages)
        {
            Publish publish(rm.topic, rm.payload, rm.qos);
            publish.retain = true;
            const MqttPacket packet(publish);
            ses->writePacket(packet, max_qos, true, count);
        }
        if (poundMode)
        {
            for (auto &pair : this_node->children)
            {
                std::unique_ptr<RetainedMessageNode> &child = pair.second;
                giveClientRetainedMessagesRecursively(cur_subtopic_it, end, child.get(), max_qos, ses, poundMode, count);
            }
        }

        return;
    }

    const std::string &cur_subtop = *cur_subtopic_it;
    const auto next_subtopic = ++cur_subtopic_it;

    bool poundFound = cur_subtop == "#";
    if (poundFound || cur_subtop == "+")
    {
        for (auto &pair : this_node->children)
        {
            std::unique_ptr<RetainedMessageNode> &child = pair.second;
            if (child) // I don't think it can ever be unset, but I'd rather avoid a crash.
                giveClientRetainedMessagesRecursively(next_subtopic, end, child.get(), max_qos, ses, poundFound, count);
        }
    }
    else
    {
        RetainedMessageNode *children = this_node->getChildren(cur_subtop);

        if (children)
        {
            giveClientRetainedMessagesRecursively(next_subtopic, end, children, max_qos, ses, false, count);
        }
    }
}

uint64_t SubscriptionStore::giveClientRetainedMessages(const std::shared_ptr<Session> &ses, const std::vector<std::string> &subscribeSubtopics, char max_qos)
{
    uint64_t count = 0;

    RetainedMessageNode *startNode = &retainedMessagesRoot;
    if (!subscribeSubtopics.empty() && !subscribeSubtopics[0].empty() > 0 && subscribeSubtopics[0][0] == '$')
        startNode = &retainedMessagesRootDollar;

    RWLockGuard locker(&retainedMessagesRwlock);
    locker.rdlock();

    giveClientRetainedMessagesRecursively(subscribeSubtopics.begin(), subscribeSubtopics.end(), startNode, max_qos, ses, false, count);

    return count;
}

void SubscriptionStore::setRetainedMessage(const std::string &topic, const std::vector<std::string> &subtopics, const std::string &payload, char qos)
{
    RetainedMessageNode *deepestNode = &retainedMessagesRoot;
    if (!subtopics.empty() && !subtopics[0].empty() > 0 && subtopics[0][0] == '$')
        deepestNode = &retainedMessagesRootDollar;

    RWLockGuard locker(&retainedMessagesRwlock);
    locker.wrlock();

    for(const std::string &subtopic : subtopics)
    {
        std::unique_ptr<RetainedMessageNode> &selectedChildren = deepestNode->children[subtopic];

        if (!selectedChildren)
        {
            selectedChildren.reset(new RetainedMessageNode());
        }
        deepestNode = selectedChildren.get();
    }

    assert(deepestNode);

    if (deepestNode)
    {
        deepestNode->addPayload(topic, payload, qos);
    }

    locker.unlock();
}

// Clean up the weak pointers to sessions and remove nodes that are empty.
int SubscriptionNode::cleanSubscriptions()
{
    int subscribersLeftInChildren = 0;
    auto childrenIt = children.begin();
    while(childrenIt != children.end())
    {
        subscribersLeftInChildren += childrenIt->second->cleanSubscriptions();

        if (subscribersLeftInChildren > 0)
            childrenIt++;
        else
        {
            Logger::getInstance()->logf(LOG_DEBUG, "Removing orphaned subscriber node from %s", childrenIt->first.c_str());
            childrenIt = children.erase(childrenIt);
        }
    }

    std::list<std::unique_ptr<SubscriptionNode>*> wildcardChildren;
    wildcardChildren.push_back(&childrenPlus);
    wildcardChildren.push_back(&childrenPound);

    for (std::unique_ptr<SubscriptionNode> *node : wildcardChildren)
    {
        std::unique_ptr<SubscriptionNode> &node_ = *node;

        if (!node_)
            continue;
        int n = node_->cleanSubscriptions();
        subscribersLeftInChildren += n;

        if (n == 0)
        {
            Logger::getInstance()->logf(LOG_DEBUG, "Resetting wildcard children");
            node_.reset();
        }
    }

    // This is not particularlly fast when it's many items. But we don't do it often, so is probably okay.
    auto it = subscribers.begin();
    while (it != subscribers.end())
    {
        if (it->sessionGone())
        {
            Logger::getInstance()->logf(LOG_DEBUG, "Removing empty spot in subscribers vector");
            it = subscribers.erase(it);
        }
        else
            it++;
    }

    return subscribers.size() + subscribersLeftInChildren;
}

// This is not MQTT compliant, but the standard doesn't keep real world constraints into account.
void SubscriptionStore::removeExpiredSessionsClients(int expireSessionsAfterSeconds)
{
    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    logger->logf(LOG_NOTICE, "Cleaning out old sessions");

    auto session_it = sessionsById.begin();
    while (session_it != sessionsById.end())
    {
        std::shared_ptr<Session> &session = session_it->second;

        if (session->hasExpired(expireSessionsAfterSeconds))
        {
            logger->logf(LOG_DEBUG, "Removing expired session from store %s", session->getClientId().c_str());
            session_it = sessionsById.erase(session_it);
        }
        else
            session_it++;
    }

    logger->logf(LOG_NOTICE, "Rebuilding subscription tree");

    root.cleanSubscriptions();
}

// QoS is not used in the comparision. This means you upgrade your QoS by subscribing again. The
// specs don't specify what to do there.
bool Subscription::operator==(const Subscription &rhs) const
{
    if (session.expired() && rhs.session.expired())
        return true;
    if (session.expired() || rhs.session.expired())
        return false;

    const std::shared_ptr<Session> lhs_ses = session.lock();
    const std::shared_ptr<Session> rhs_ses = rhs.session.lock();

    return lhs_ses->getClientId() == rhs_ses->getClientId();
}

void Subscription::reset()
{
    session.reset();
    qos = 0;
}

bool Subscription::sessionGone() const
{
    return session.expired();
}

void RetainedMessageNode::addPayload(const std::string &topic, const std::string &payload, char qos)
{
    RetainedMessage rm(topic, payload, qos);

    auto retained_ptr = retainedMessages.find(rm);
    bool retained_found = retained_ptr != retainedMessages.end();

    if (!retained_found && payload.empty())
        return;

    if (retained_found && payload.empty())
    {
        retainedMessages.erase(rm);
        return;
    }

    if (retained_found)
        retainedMessages.erase(rm);

    retainedMessages.insert(std::move(rm));
}

/**
 * @brief RetainedMessageNode::getChildren return the children or nullptr when there are none. Const, so doesn't default construct.
 * @param subtopic
 * @return
 */
RetainedMessageNode *RetainedMessageNode::getChildren(const std::string &subtopic) const
{
    auto it = children.find(subtopic);
    if (it != children.end())
        return it->second.get();
    return nullptr;
}
