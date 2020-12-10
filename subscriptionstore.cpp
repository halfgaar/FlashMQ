#include "subscriptionstore.h"

SubscriptionStore::SubscriptionStore()
{

}

void SubscriptionStore::addSubscription(Client_p &client, std::string &topic)
{
    std::lock_guard<std::mutex> lock(subscriptionsMutex);
    this->subscriptions[topic].push_back(client);
}

void SubscriptionStore::queueAtClientsTemp(std::string &topic, const MqttPacket &packet, const Client_p &sender)
{
    // TODO: temp. I want to work with read copies of the subscription store, to avoid frequent lock contention.
    std::lock_guard<std::mutex> lock(subscriptionsMutex);

    for(Client_p &client : subscriptions[topic])
    {
        if (client->getThreadData()->threadnr == sender->getThreadData()->threadnr)
        {
            client->writeMqttPacket(packet); // TODO: with my current hack way, this is wrong. Not using a lock only works with my previous idea of queueing.
            client->writeBufIntoFd();
        }
        else
        {
            client->writeMqttPacketLocked(packet);
            client->getThreadData()->addToReadyForDequeuing(client);
            client->getThreadData()->wakeUpThread();
        }
    }
}
