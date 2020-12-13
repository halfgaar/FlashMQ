#include "subscriptionstore.h"

#include "cassert"


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
    std::lock_guard<std::mutex> lock(subscriptionsMutex);

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
}

void SubscriptionStore::removeClient(const Client_p &client)
{

}

void SubscriptionStore::queueAtClientsTemp(std::string &topic, const MqttPacket &packet, const Client_p &sender)
{
    const std::list<std::string> subtopics = split(topic, '/');

    // TODO: temp. I want to work with read copies of the subscription store, to avoid frequent lock contention.
    std::lock_guard<std::mutex> lock(subscriptionsMutex);

    SubscriptionNode *deepestNode = subscriptions2.get();
    for(const std::string &subtopic : subtopics)
    {
        SubscriptionNode &nodeRef = *deepestNode;

        if (nodeRef.children.count(subtopic) == 0)
            return;

        std::unique_ptr<SubscriptionNode> &node = nodeRef.children[subtopic];

        assert(node); // because any empty unique_ptr's is a bug

        for (const std::string &client_id : node->subscribers)
        {

        }

        deepestNode = node.get();

    }

    /*
    for(const std::string &subtopic : subtopics)
    {
        std::unique_ptr<SubscriptionNode> &node = subscriptions2[subtopic];

        if (!node)
        {
            subscriptions2
        }
    }*/

    /*
    for(const Client_p &client : subscriptions2[topic])
    {
        client->writeMqttPacket(packet);


        if (client->getThreadData()->threadnr == sender->getThreadData()->threadnr)
        {
            client->writeMqttPacket(packet); // TODO: with my current hack way, this is wrong. Not using a lock only works with my previous idea of queueing.
        }
        else
        {
            // Or keep a list of queued messages in the store, per client?

            //client->writeMqttPacketLocked(packet);
            //client->getThreadData()->addToReadyForDequeuing(client);
            //client->getThreadData()->wakeUpThread();
        }
    }*/
}


