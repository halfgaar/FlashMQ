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
#include <map>
#include <vector>
#include <pthread.h>

#include "forward_declarations.h"

#include "client.h"
#include "session.h"
#include "utils.h"
#include "retainedmessage.h"
#include "logger.h"


struct Subscription
{
    std::weak_ptr<Session> session; // Weak pointer expires when session has been cleaned by 'clean session' connect or when it was remove because it expired
    uint8_t qos;
    bool operator==(const Subscription &rhs) const;
    void reset();
};

struct ReceivingSubscriber
{
    const std::shared_ptr<Session> session;
    const uint8_t qos;

public:
    ReceivingSubscriber(const std::shared_ptr<Session> &ses, uint8_t qos);
};

class SubscriptionNode
{
    std::string subtopic;
    std::unordered_map<std::string, Subscription> subscribers;

public:
    SubscriptionNode(const std::string &subtopic);
    SubscriptionNode(const SubscriptionNode &node) = delete;
    SubscriptionNode(SubscriptionNode &&node) = delete;

    std::unordered_map<std::string, Subscription> &getSubscribers();
    const std::string &getSubtopic() const;
    void addSubscriber(const std::shared_ptr<Session> &subscriber, uint8_t qos);
    void removeSubscriber(const std::shared_ptr<Session> &subscriber);
    std::unordered_map<std::string, std::unique_ptr<SubscriptionNode>> children;
    std::unique_ptr<SubscriptionNode> childrenPlus;
    std::unique_ptr<SubscriptionNode> childrenPound;

    SubscriptionNode *getChildren(const std::string &subtopic) const;

    int cleanSubscriptions();
};

class RetainedMessageNode
{
    friend class SubscriptionStore;

    std::unordered_map<std::string, std::unique_ptr<RetainedMessageNode>> children;
    std::unordered_set<RetainedMessage> retainedMessages;

    void addPayload(const Publish &publish, int64_t &totalCount);
    RetainedMessageNode *getChildren(const std::string &subtopic) const;
    bool isOrphaned() const;
};

class QueuedWill
{
    std::weak_ptr<WillPublish> will;
    std::weak_ptr<Session> session;

public:
    QueuedWill(const std::shared_ptr<WillPublish> &will, const std::shared_ptr<Session> &session);

    const std::weak_ptr<WillPublish> &getWill() const;
    std::shared_ptr<Session> getSession();
};

class SubscriptionStore
{
#ifdef TESTING
    friend class MainTests;
#endif

    SubscriptionNode root;
    SubscriptionNode rootDollar;
    pthread_rwlock_t sessionsAndSubscriptionsRwlock = PTHREAD_RWLOCK_INITIALIZER;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessionsById;
    const std::unordered_map<std::string, std::shared_ptr<Session>> &sessionsByIdConst;

    std::mutex queuedSessionRemovalsMutex;
    std::map<std::chrono::seconds, std::vector<std::weak_ptr<Session>>> queuedSessionRemovals;

    pthread_rwlock_t retainedMessagesRwlock = PTHREAD_RWLOCK_INITIALIZER;
    RetainedMessageNode retainedMessagesRoot;
    RetainedMessageNode retainedMessagesRootDollar;
    int64_t retainedMessageCount = 0;

    int64_t subscriptionCount = 0;
    std::chrono::time_point<std::chrono::steady_clock> lastSubscriptionCountRefreshedAt;

    std::mutex pendingWillsMutex;
    std::map<std::chrono::seconds, std::vector<QueuedWill>> pendingWillMessages;

    std::chrono::time_point<std::chrono::steady_clock> lastTreeCleanup;

    Logger *logger = Logger::getInstance();

    static void publishNonRecursively(const std::unordered_map<std::string, Subscription> &subscribers,
                               std::forward_list<ReceivingSubscriber> &targetSessions);
    static void publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                            SubscriptionNode *this_node, std::forward_list<ReceivingSubscriber> &targetSessions);
    static void giveClientRetainedMessagesRecursively(std::vector<std::string>::const_iterator cur_subtopic_it,
                                               std::vector<std::string>::const_iterator end, RetainedMessageNode *this_node, bool poundMode,
                                               std::forward_list<Publish> &packetList);
    void getRetainedMessages(RetainedMessageNode *this_node, std::vector<RetainedMessage> &outputList) const;
    void getSubscriptions(SubscriptionNode *this_node, const std::string &composedTopic, bool root,
                          std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &outputList) const;
    void countSubscriptions(SubscriptionNode *this_node, int64_t &count) const;
    void expireRetainedMessages(RetainedMessageNode *this_node, const std::chrono::time_point<std::chrono::steady_clock> &limit);

    SubscriptionNode *getDeepestNode(const std::string &topic, const std::vector<std::string> &subtopics);
public:
    SubscriptionStore();

    void addSubscription(std::shared_ptr<Client> &client, const std::string &topic, const std::vector<std::string> &subtopics, uint8_t qos);
    void removeSubscription(std::shared_ptr<Client> &client, const std::string &topic);
    void registerClientAndKickExistingOne(std::shared_ptr<Client> &client);
    void registerClientAndKickExistingOne(std::shared_ptr<Client> &client, bool clean_start, uint16_t clientReceiveMax, uint32_t sessionExpiryInterval);
    std::shared_ptr<Session> lockSession(const std::string &clientid);

    void sendQueuedWillMessages();
    void queueWillMessage(const std::shared_ptr<WillPublish> &willMessage, const std::shared_ptr<Session> &session, bool forceNow = false);
    void queuePacketAtSubscribers(PublishCopyFactory &copyFactory, bool dollar = false);
    void giveClientRetainedMessages(const std::shared_ptr<Session> &ses,
                                    const std::vector<std::string> &subscribeSubtopics, uint8_t max_qos);

    void setRetainedMessage(const Publish &publish, const std::vector<std::string> &subtopics);

    void removeSession(const std::shared_ptr<Session> &session);
    void removeExpiredSessionsClients();
    void expireRetainedMessages();

    int64_t getRetainedMessageCount() const;
    uint64_t getSessionCount() const;
    int64_t getSubscriptionCount();

    void saveRetainedMessages(const std::string &filePath);
    void loadRetainedMessages(const std::string &filePath);

    void saveSessionsAndSubscriptions(const std::string &filePath);
    void loadSessionsAndSubscriptions(const std::string &filePath);

    void queueSessionRemoval(const std::shared_ptr<Session> &session);
};

#endif // SUBSCRIPTIONSTORE_H
