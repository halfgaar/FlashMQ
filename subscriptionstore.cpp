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

void SubscriptionStore::addSubscription(Client_p &client, std::string &topic)
{
    const std::list<std::string> subtopics = split(topic, '/');

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    SubscriptionNode *deepestNode = root.get();
    for(const std::string &subtopic : subtopics)
    {
        SubscriptionNode &nodeRef = *deepestNode;
        std::unique_ptr<SubscriptionNode> &node = nodeRef.children[subtopic];

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
}

void SubscriptionStore::removeClient(const Client_p &client)
{
    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();
    clients_by_id.erase(client->getClientId());
}

// TODO: keep a cache of topics vs clients

bool SubscriptionStore::publishRecursively(std::list<std::string>::const_iterator cur_subtopic_it, std::list<std::string>::const_iterator end,
                                           std::unique_ptr<SubscriptionNode> &this_node, const MqttPacket &packet) const
{
    if (cur_subtopic_it == end) // This is the end of the topic path, so look for subscribers here.
    {
        for (const std::string &client_id : this_node->subscribers)
        {
            auto client_it = clients_by_id_const.find(client_id);
            if (client_it != clients_by_id_const.end())
                client_it->second->writeMqttPacket(packet);
        }

        return true;
    }

    std::string cur_subtop = *cur_subtopic_it;
    auto sub_node = this_node->children.find(cur_subtop);

    const auto next_subtopic = ++cur_subtopic_it;

    if (sub_node != this_node->children.end())
    {
        publishRecursively(next_subtopic, end, sub_node->second, packet);
    }

    const auto plus_sign_node = this_node->children.find("+");

    if (plus_sign_node != this_node->children.end())
    {
        publishRecursively(next_subtopic, end, plus_sign_node->second, packet);
    }

    return false;
}

void SubscriptionStore::queuePacketAtSubscribers(std::string &topic, const MqttPacket &packet, const Client_p &sender)
{
    const std::list<std::string> subtopics = split(topic, '/');

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.rdlock();

    publishRecursively(subtopics.begin(), subtopics.end(), root, packet);
}


