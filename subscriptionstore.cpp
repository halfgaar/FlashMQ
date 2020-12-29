#include "subscriptionstore.h"

#include "cassert"

#include "rwlockguard.h"


SubscriptionNode::SubscriptionNode(const std::string &subtopic) :
    subtopic(subtopic)
{

}

SubscriptionStore::SubscriptionStore() :
    root(new SubscriptionNode("root")),
    clients_by_id_const(clients_by_id)
{

}

void SubscriptionStore::addSubscription(Client_p &client, const std::string &topic)
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

    if (deepestNode)
    {
        deepestNode->subscribers.push_front(client->getClientId());
    }

    clients_by_id[client->getClientId()] = client;
    lock_guard.unlock();

    giveClientRetainedMessages(client, topic);
}

// TODO: should I implement cache, this needs to be changed to returning a list of clients.
void SubscriptionStore::publishNonRecursively(const MqttPacket &packet, const std::forward_list<std::string> &subscribers) const
{
    for (const std::string &client_id : subscribers)
    {
        auto client_it = clients_by_id_const.find(client_id);
        if (client_it != clients_by_id_const.end())
        {
            if (!client_it->second.expired())
            {
                Client_p c = client_it->second.lock();
                c->writeMqttPacketAndBlameThisClient(packet);
            }
        }
    }
}

void SubscriptionStore::publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                                           std::unique_ptr<SubscriptionNode> &this_node, const MqttPacket &packet) const
{
    if (cur_subtopic_it == end) // This is the end of the topic path, so look for subscribers here.
    {
        publishNonRecursively(packet, this_node->subscribers);
        return;
    }

    if (this_node->children.empty() && !this_node->childrenPlus && !this_node->childrenPound)
        return;

    std::string cur_subtop = *cur_subtopic_it;

    const auto next_subtopic = ++cur_subtopic_it;

    if (this_node->childrenPound)
    {
        publishNonRecursively(packet, this_node->childrenPound->subscribers);
    }

    auto sub_node = this_node->children.find(cur_subtop);
    if (sub_node != this_node->children.end())
    {
        publishRecursively(next_subtopic, end, sub_node->second, packet);
    }

    if (this_node->childrenPlus)
    {
        publishRecursively(next_subtopic, end, this_node->childrenPlus, packet);
    }
}

void SubscriptionStore::queuePacketAtSubscribers(const std::string &topic, const MqttPacket &packet, const Client_p &sender)
{
    // TODO: keep a cache of topics vs clients

    const std::vector<std::string> subtopics = splitToVector(topic, '/');

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.rdlock();

    publishRecursively(subtopics.begin(), subtopics.end(), root, packet);
}

void SubscriptionStore::giveClientRetainedMessages(Client_p &client, const std::string &subscribe_topic)
{
    RWLockGuard locker(&retainedMessagesRwlock);
    locker.rdlock();

    for(const RetainedMessage &rm : retainedMessages)
    {
        Publish publish(rm.topic, rm.payload, rm.qos);
        publish.retain = true;
        const MqttPacket packet(publish);

        if (topicsMatch(subscribe_topic, rm.topic))
            client->writeMqttPacket(packet);
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


