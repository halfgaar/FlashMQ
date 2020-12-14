#include "subscriptionstore.h"

#include "cassert"

#include "rwlockguard.h"


SubscriptionNode::SubscriptionNode(const std::string &subtopic) :
    subtopic(subtopic)
{

}

SubscriptionStore::SubscriptionStore() :
    subscriptions2(new SubscriptionNode("root"))
{

}

void SubscriptionStore::addSubscription(Client_p &client, std::string &topic)
{
    const std::list<std::string> subtopics = split(topic, '/');

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();

    SubscriptionNode *deepestNode = subscriptions2.get();
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
        deepestNode->subscribers.insert(client->getClientId());
    }

    clients_by_id[client->getClientId()] = client;
}

void SubscriptionStore::removeClient(const Client_p &client)
{
    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.wrlock();
    clients_by_id.erase(client->getClientId());
}

void SubscriptionStore::queueAtClientsTemp(std::string &topic, const MqttPacket &packet, const Client_p &sender)
{
    const std::list<std::string> subtopics = split(topic, '/');
    const auto &clients = clients_by_id;

    RWLockGuard lock_guard(&subscriptionsRwlock);
    lock_guard.rdlock();

    const SubscriptionNode *deepestNode = subscriptions2.get();
    for(const std::string &subtopic : subtopics)
    {
        auto sub_iter = deepestNode->children.find(subtopic);
        if (sub_iter == deepestNode->children.end())
            return;

        const std::unique_ptr<SubscriptionNode> &sub_node = sub_iter->second;
        assert(sub_node); // because any empty unique_ptr's is a bug
        deepestNode = sub_node.get();
    }

    for (const std::string &client_id : deepestNode->subscribers)
    {
        std::cout << "Publishing to " << client_id << std::endl;
        auto client_it = clients.find(client_id);
        if (client_it != clients.end())
            client_it->second->writeMqttPacket(packet);
    }
}


