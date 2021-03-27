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


SubscriptionStore::SubscriptionStore() :
    root(new SubscriptionNode("root")),
    sessionsByIdConst(sessionsById)
{

}

void SubscriptionStore::addSubscription(Client_p &client, const std::string &topic, char qos)
{
    const std::list<std::string> subtopics = split(topic, '/');

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    SubscriptionNode *deepestNode = root.get();
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
            giveClientRetainedMessages(ses, topic, qos);
        }
    }

    lock_guard.unlock();


}

void SubscriptionStore::removeSubscription(Client_p &client, const std::string &topic)
{
    const std::list<std::string> subtopics = split(topic, '/');

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    // TODO: because it's so similar to adding a subscription, make a function to retrieve the deepest node?
    SubscriptionNode *deepestNode = root.get();
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
            return;
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
            deepestNode->removeSubscriber(ses);
        }
    }

    lock_guard.unlock();


}

// Removes an existing client when it already exists [MQTT-3.1.4-2].
void SubscriptionStore::registerClientAndKickExistingOne(Client_p &client)
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
    session->sendPendingQosMessages();
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

// TODO: should I implement cache, this needs to be changed to returning a list of clients.
void SubscriptionStore::publishNonRecursively(const MqttPacket &packet, const std::vector<Subscription> &subscribers) const
{
    for (const Subscription &sub : subscribers)
    {
        std::weak_ptr<Session> session_weak = sub.session;
        if (!session_weak.expired()) // Shared pointer expires when session has been cleaned by 'clean session' connect.
        {
            const std::shared_ptr<Session> session = session_weak.lock();
            session->writePacket(packet, sub.qos);
        }
    }
}

void SubscriptionStore::publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                                           std::unique_ptr<SubscriptionNode> &this_node, const MqttPacket &packet) const
{
    if (cur_subtopic_it == end) // This is the end of the topic path, so look for subscribers here.
    {
        if (this_node)
            publishNonRecursively(packet, this_node->getSubscribers());
        return;
    }

    if (this_node->children.empty() && !this_node->childrenPlus && !this_node->childrenPound)
        return;

    const std::string &cur_subtop = *cur_subtopic_it;

    const auto next_subtopic = ++cur_subtopic_it;

    if (this_node->childrenPound)
    {
        publishNonRecursively(packet, this_node->childrenPound->getSubscribers());
    }

    const auto &sub_node = this_node->children.find(cur_subtop);
    if (sub_node != this_node->children.end())
    {
        publishRecursively(next_subtopic, end, sub_node->second, packet);
    }

    if (this_node->childrenPlus)
    {
        publishRecursively(next_subtopic, end, this_node->childrenPlus, packet);
    }
}

void SubscriptionStore::queuePacketAtSubscribers(const std::string &topic, const MqttPacket &packet)
{
    // TODO: keep a cache of topics vs clients

    const std::vector<std::string> subtopics = splitToVector(topic, '/');

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.rdlock();

    publishRecursively(subtopics.begin(), subtopics.end(), root, packet);
}

void SubscriptionStore::giveClientRetainedMessages(const std::shared_ptr<Session> &ses, const std::string &subscribe_topic, char max_qos)
{
    RWLockGuard locker(&retainedMessagesRwlock);
    locker.rdlock();

    for(const RetainedMessage &rm : retainedMessages)
    {
        Publish publish(rm.topic, rm.payload, rm.qos);
        publish.retain = true;
        const MqttPacket packet(publish);

        if (topicsMatch(subscribe_topic, rm.topic))
            ses->writePacket(packet, max_qos);
    }
}

void SubscriptionStore::setRetainedMessage(const std::string &topic, const std::string &payload, char qos)
{
    RWLockGuard locker(&retainedMessagesRwlock);
    locker.wrlock();

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
void SubscriptionStore::removeExpiredSessionsClients()
{
    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    logger->logf(LOG_NOTICE, "Cleaning out old sessions");

    auto session_it = sessionsById.begin();
    while (session_it != sessionsById.end())
    {
        std::shared_ptr<Session> &session = session_it->second;

        if (session->hasExpired())
        {
#ifndef NDEBUG
            logger->logf(LOG_DEBUG, "Removing expired session from store %s", session->getClientId().c_str());
#endif
            session_it = sessionsById.erase(session_it);
        }
        else
            session_it++;
    }

    logger->logf(LOG_NOTICE, "Rebuilding subscription tree");

    root->cleanSubscriptions();
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
