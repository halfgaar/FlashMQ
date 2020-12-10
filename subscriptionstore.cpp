#include "subscriptionstore.h"

SubscriptionStore::SubscriptionStore()
{

}

void SubscriptionStore::addSubscription(Client_p &client, std::string &topic)
{
    this->subscriptions[topic].push_back(client);
}
