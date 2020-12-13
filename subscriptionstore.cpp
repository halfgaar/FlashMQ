#include "subscriptionstore.h"

SubscriptionStore::SubscriptionStore()
{

}

void SubscriptionStore::addSubscription(Client_p &client, std::string &topic)
{
    std::lock_guard<std::mutex> lock(subscriptionsMutex);
    this->subscriptions[topic].insert(client);
}

void SubscriptionStore::removeClient(const Client_p &client)
{
    std::lock_guard<std::mutex> lock(subscriptionsMutex);
    for(std::pair<const std::string, std::unordered_set<Client_p>> &pair : subscriptions)
    {
        std::unordered_set<Client_p> &bla = pair.second;
        bla.erase(client);
    }
}

void SubscriptionStore::queueAtClientsTemp(std::string &topic, const MqttPacket &packet, const Client_p &sender)
{
    // TODO: temp. I want to work with read copies of the subscription store, to avoid frequent lock contention.
    std::lock_guard<std::mutex> lock(subscriptionsMutex);

    for(const Client_p &client : subscriptions[topic])
    {
        client->writeMqttPacket(packet);

        /*
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
        }*/
    }
}
