/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

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
    std::weak_ptr<Session> session; // Weak pointer expires when session has been cleaned by 'clean session' connect or when it was remove because it expired
    char qos;
    bool operator==(const Subscription &rhs) const;
    void reset();
    bool sessionGone() const;
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
    void removeSubscriber(const std::shared_ptr<Session> &subscriber);
    std::unordered_map<std::string, std::unique_ptr<SubscriptionNode>> children;
    std::unique_ptr<SubscriptionNode> childrenPlus;
    std::unique_ptr<SubscriptionNode> childrenPound;

    int cleanSubscriptions();
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

    void addSubscription(std::shared_ptr<Client> &client, const std::string &topic, char qos);
    void removeSubscription(std::shared_ptr<Client> &client, const std::string &topic);
    void registerClientAndKickExistingOne(std::shared_ptr<Client> &client);
    bool sessionPresent(const std::string &clientid);

    void queuePacketAtSubscribers(const std::vector<std::string> &subtopics, const MqttPacket &packet);
    void giveClientRetainedMessages(const std::shared_ptr<Session> &ses, const std::string &subscribe_topic, char max_qos);

    void setRetainedMessage(const std::string &topic, const std::string &payload, char qos);

    void removeExpiredSessionsClients();
};

#endif // SUBSCRIPTIONSTORE_H
