#ifndef SUBSCRIPTIONSTORE_H
#define SUBSCRIPTIONSTORE_H

#include <unordered_map>
#include <forward_list>
#include <list>
#include <mutex>
#include <pthread.h>

#include "forward_declarations.h"

#include "client.h"
#include "session.h"
#include "utils.h"
#include "retainedmessage.h"
#include "logger.h"

struct RetainedPayload
{
    std::string payload;
    char qos;
};

class SubscriptionNode
{
    std::string subtopic;

public:
    SubscriptionNode(const std::string &subtopic);
    SubscriptionNode(const SubscriptionNode &node) = delete;
    SubscriptionNode(SubscriptionNode &&node) = delete;

    std::forward_list<std::weak_ptr<Session>> subscribers; // The idea is to store subscriptions by client id, to support persistent sessions.
    std::unordered_map<std::string, std::unique_ptr<SubscriptionNode>> children;
    std::unique_ptr<SubscriptionNode> childrenPlus;
    std::unique_ptr<SubscriptionNode> childrenPound;
};

class SubscriptionStore
{
    std::unique_ptr<SubscriptionNode> root;
    pthread_rwlock_t subscriptionsRwlock = PTHREAD_RWLOCK_INITIALIZER;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessionsById;
    const std::unordered_map<std::string, std::shared_ptr<Session>> &sessionsByIdConst;

    pthread_rwlock_t retainedMessagesRwlock = PTHREAD_RWLOCK_INITIALIZER;
    std::unordered_set<RetainedMessage> retainedMessages;

    Logger *logger = Logger::getInstance();

    void publishNonRecursively(const MqttPacket &packet, const std::forward_list<std::weak_ptr<Session>> &subscribers) const;
    void publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                            std::unique_ptr<SubscriptionNode> &next, const MqttPacket &packet) const;
public:
    SubscriptionStore();

    void addSubscription(Client_p &client, const std::string &topic);
    void registerClientAndKickExistingOne(Client_p &client);

    void queuePacketAtSubscribers(const std::string &topic, const MqttPacket &packet, const Client_p &sender);
    void giveClientRetainedMessages(Client_p &client, const std::string &subscribe_topic);

    void setRetainedMessage(const std::string &topic, const std::string &payload, char qos);
};

#endif // SUBSCRIPTIONSTORE_H
