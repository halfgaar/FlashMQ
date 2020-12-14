#ifndef SUBSCRIPTIONSTORE_H
#define SUBSCRIPTIONSTORE_H

#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <pthread.h>

#include "forward_declarations.h"

#include "client.h"
#include "utils.h"

class SubscriptionNode
{
    std::string subtopic;

public:
    SubscriptionNode(const std::string &subtopic);
    SubscriptionNode(const SubscriptionNode &node) = delete;
    SubscriptionNode(SubscriptionNode &&node) = delete;

    std::unordered_set<std::string> subscribers; // The idea is to store subscriptions by client id, to support persistent sessions.
    std::unordered_map<std::string, std::unique_ptr<SubscriptionNode>> children;

};


class SubscriptionStore
{
    std::unique_ptr<SubscriptionNode> subscriptions2;
    pthread_rwlock_t subscriptionsRwlock = PTHREAD_RWLOCK_INITIALIZER;
    std::unordered_map<std::string, Client_p> clients_by_id;
public:
    SubscriptionStore();

    void addSubscription(Client_p &client, std::string &topic);
    void removeClient(const Client_p &client);

    // work with read copies intead of mutex/lock over the central store
    void getReadCopy(); // TODO

    void queueAtClientsTemp(std::string &topic, const MqttPacket &packet, const Client_p &sender);
};

#endif // SUBSCRIPTIONSTORE_H
