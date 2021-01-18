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

struct Subscription
{
    std::weak_ptr<Session> session; // Weak pointer expires when session has been cleaned by 'clean session' connect.
    char qos;
    bool operator==(const Subscription &rhs) const;
    void reset();
};

class SubscriptionNode
{
    std::string subtopic;
    std::vector<Subscription> subscribers;

public:
    SubscriptionNode(const std::string &subtopic);
    SubscriptionNode(const SubscriptionNode &node) = delete;
    SubscriptionNode(SubscriptionNode &&node) = delete;

    std::vector<Subscription> &getSubscribers();
    void addSubscriber(const std::shared_ptr<Session> &subscriber, char qos);
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

    void publishNonRecursively(const MqttPacket &packet, const std::vector<Subscription> &subscribers) const;
    void publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                            std::unique_ptr<SubscriptionNode> &next, const MqttPacket &packet) const;
public:
    SubscriptionStore();

    void addSubscription(Client_p &client, const std::string &topic, char qos);
    void registerClientAndKickExistingOne(Client_p &client);

    void queuePacketAtSubscribers(const std::string &topic, const MqttPacket &packet, const Client_p &sender);
    void giveClientRetainedMessages(const std::shared_ptr<Session> &ses, const std::string &subscribe_topic, char max_qos);

    void setRetainedMessage(const std::string &topic, const std::string &payload, char qos);
};

#endif // SUBSCRIPTIONSTORE_H
