#include "subscriptionstore.h"

SubscriptionStore::SubscriptionStore()
{

}

void SubscriptionStore::addSubscription(Client_p &client, std::string &topic)
{
    this->subscriptions[topic].push_back(client);
}

void SubscriptionStore::queueAtClientsTemp(std::string &topic, const MqttPacket &packet, const Client_p &sender)
{
    for(Client_p &client : subscriptions[topic])
    {
        if (client->getThreadData()->threadnr == sender->getThreadData()->threadnr)
        {
            client->writeMqttPacket(packet);
            client->writeBufIntoFd();
        }
        else
        {
            client->queueMessage(packet);
            client->getThreadData()->addToReadyForDequeuing(client);
        }
    }
}
