#ifndef SUBSCRIPTIONSTORE_H
#define SUBSCRIPTIONSTORE_H

#include <unordered_map>
#include <list>
#include <mutex>

#include "forward_declarations.h"

#include "client.h"

class SubscriptionStore
{
    std::unordered_map<std::string, std::unordered_set<Client_p>> subscriptions;
    std::mutex subscriptionsMutex;
public:
    SubscriptionStore();

    void addSubscription(Client_p &client, std::string &topic);
    void removeClient(const Client_p &client);

    // work with read copies intead of mutex/lock over the central store
    void getReadCopy(); // TODO

    void queueAtClientsTemp(std::string &topic, const MqttPacket &packet, const Client_p &sender);
};

#endif // SUBSCRIPTIONSTORE_H
