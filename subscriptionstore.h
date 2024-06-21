/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
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
#include <optional>

#include "client.h"
#include "session.h"
#include "retainedmessage.h"
#include "logger.h"
#include "subscription.h"
#include "sharedsubscribers.h"


struct ReceivingSubscriber
{
    const std::shared_ptr<Session> session;
    const uint8_t qos;
    const bool retainAsPublished;

public:
    ReceivingSubscriber(const std::weak_ptr<Session> &ses, uint8_t qos, bool retainAsPublished);
};

class SubscriptionNode
{
    friend class SubscriptionStore;

    std::unordered_map<std::string, Subscription> subscribers;
    std::unordered_map<std::string, SharedSubscribers> sharedSubscribers;
    std::shared_mutex lock;

    std::chrono::time_point<std::chrono::steady_clock> lastUpdate;

public:
    SubscriptionNode();
    SubscriptionNode(const SubscriptionNode &node) = delete;
    SubscriptionNode(SubscriptionNode &&node) = delete;

    const std::unordered_map<std::string, Subscription> &getSubscribers() const;
    std::unordered_map<std::string, SharedSubscribers> &getSharedSubscribers();
    void addSubscriber(const std::shared_ptr<Session> &subscriber, uint8_t qos, bool noLocal, bool retainAsPublished, const std::string &shareName);
    void removeSubscriber(const std::shared_ptr<Session> &subscriber, const std::string &shareName);
    std::unordered_map<std::string, std::shared_ptr<SubscriptionNode>> children;
    std::shared_ptr<SubscriptionNode> childrenPlus;
    std::shared_ptr<SubscriptionNode> childrenPound;

    int cleanSubscriptions(std::deque<std::weak_ptr<SubscriptionNode>> &defferedLeafs);
    bool empty() const;
};

class RetainedMessageNode
{
    friend class SubscriptionStore;

    std::unordered_map<std::string, std::shared_ptr<RetainedMessageNode>> children;
    std::mutex messageSetMutex;
    std::unique_ptr<RetainedMessage> message;
    std::chrono::time_point<std::chrono::steady_clock> messageSetAt;

    void addPayload(const Publish &publish, int64_t &totalCount);
    std::shared_ptr<RetainedMessageNode> getChildren(const std::string &subtopic) const;
    bool isOrphaned() const;
    const std::chrono::time_point<std::chrono::steady_clock> getMessageSetAt() const;
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

struct DeferredRetainedMessageNodeDelivery
{
    std::weak_ptr<RetainedMessageNode> node;
    std::vector<std::string>::const_iterator cur;
    std::vector<std::string>::const_iterator end;
    bool poundMode = false;
};

struct DeferredGetSubscription
{
    const std::weak_ptr<SubscriptionNode> node;
    const std::string composedTopic;
    const bool root = false;

    DeferredGetSubscription(const std::shared_ptr<SubscriptionNode> &node, const std::string &composedTopic, const bool root);
};

class SubscriptionStore
{
#ifdef TESTING
    friend class MainTests;
#endif

    const std::shared_ptr<SubscriptionNode> root = std::make_shared<SubscriptionNode>();
    const std::shared_ptr<SubscriptionNode> rootDollar = std::make_shared<SubscriptionNode>();
    std::shared_mutex subscriptions_lock;
    std::shared_mutex sessions_lock;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessionsById;
    const std::unordered_map<std::string, std::shared_ptr<Session>> &sessionsByIdConst;

    std::mutex queuedSessionRemovalsMutex;
    std::map<std::chrono::seconds, std::vector<std::weak_ptr<Session>>> queuedSessionRemovals;

    pthread_rwlock_t retainedMessagesRwlock = PTHREAD_RWLOCK_INITIALIZER;
    std::deque<std::weak_ptr<RetainedMessageNode>> deferredRetainedMessageNodeToPurge;
    const std::shared_ptr<RetainedMessageNode> retainedMessagesRoot = std::make_shared<RetainedMessageNode>();
    const std::shared_ptr<RetainedMessageNode> retainedMessagesRootDollar = std::make_shared<RetainedMessageNode>();
    int64_t retainedMessageCount = 0;

    int64_t subscriptionCount = 0;
    std::chrono::time_point<std::chrono::steady_clock> lastSubscriptionCountRefreshedAt;

    std::mutex pendingWillsMutex;
    std::map<std::chrono::seconds, std::vector<QueuedWill>> pendingWillMessages;

    std::deque<std::weak_ptr<SubscriptionNode>> deferredSubscriptionLeafsForPurging;

    Logger *logger = Logger::getInstance();

    static void publishNonRecursively(SubscriptionNode *this_node,
                                      std::forward_list<ReceivingSubscriber> &targetSessions, size_t distributionHash, const std::string &senderClientId);
    static void publishRecursively(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                            SubscriptionNode *this_node, std::forward_list<ReceivingSubscriber> &targetSessions, size_t distributionHash, const std::string &senderClientId);
    static void giveClientRetainedMessagesRecursively(std::vector<std::string>::const_iterator cur_subtopic_it,
                                                      std::vector<std::string>::const_iterator end, const std::shared_ptr<RetainedMessageNode> &this_node, bool poundMode,
                                                      const std::shared_ptr<Session> &session, const uint8_t max_qos,
                                                      const std::chrono::time_point<std::chrono::steady_clock> &limit,
                                                      std::deque<DeferredRetainedMessageNodeDelivery> &deferred,
                                                      int &drop_count, int &processed_nodes_count);
    void getRetainedMessages(RetainedMessageNode *this_node, std::vector<RetainedMessage> &outputList,
                             const std::chrono::time_point<std::chrono::steady_clock> &limit,
                             std::deque<std::weak_ptr<RetainedMessageNode>> &deferred) const;
    void getSubscriptions(SubscriptionNode *this_node, const std::string &composedTopic, bool root,
                          std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &outputList,
                          std::deque<DeferredGetSubscription> &deferred, const std::chrono::time_point<std::chrono::steady_clock> limit) const;
    std::unordered_map<std::string, std::list<SubscriptionForSerializing>> getSubscriptions();
    void countSubscriptions(SubscriptionNode *this_node, int64_t &count) const;
    void expireRetainedMessages(RetainedMessageNode *this_node, const std::chrono::time_point<std::chrono::steady_clock> &limit,
                                std::deque<std::weak_ptr<RetainedMessageNode>> &deferred);

    std::shared_ptr<SubscriptionNode> getDeepestNode(const std::vector<std::string> &subtopics, bool abort_on_dead_end=false);
public:
    SubscriptionStore();

    void addSubscription(std::shared_ptr<Client> &client, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished);
    void addSubscription(std::shared_ptr<Client> &client, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished,
                         const std::string &shareName, AuthResult authResult);
    void removeSubscription(std::shared_ptr<Client> &client, const std::string &topic);
    std::shared_ptr<Session> getBridgeSession(std::shared_ptr<Client> &client);
    void registerClientAndKickExistingOne(std::shared_ptr<Client> &client);
    void registerClientAndKickExistingOne(std::shared_ptr<Client> &client, bool clean_start, uint16_t clientReceiveMax, uint32_t sessionExpiryInterval);
    std::shared_ptr<Session> lockSession(const std::string &clientid);

    void sendQueuedWillMessages();
    void queueWillMessage(const std::shared_ptr<WillPublish> &willMessage, const std::string &senderClientId, const std::shared_ptr<Session> &session, bool forceNow = false);
    void queuePacketAtSubscribers(PublishCopyFactory &copyFactory, const std::string &senderClientId, bool dollar = false);
    void giveClientRetainedMessages(const std::shared_ptr<Session> &ses,
                                    const std::vector<std::string> &subscribeSubtopics, uint8_t max_qos);
    void giveClientRetainedMessagesInitiateDeferred(const std::weak_ptr<Session> ses,
                                                    const std::shared_ptr<const std::vector<std::string>> subscribeSubtopicsCopy,
                                                    std::shared_ptr<std::deque<DeferredRetainedMessageNodeDelivery>> deferred,
                                                    int &requeue_count, uint &total_node_count, uint8_t max_qos);

    void trySetRetainedMessages(const Publish &publish, const std::vector<std::string> &subtopics);
    bool setRetainedMessage(const Publish &publish, const std::vector<std::string> &subtopics, bool try_lock_fail=false);

    void removeSession(const std::shared_ptr<Session> &session);
    void removeExpiredSessionsClients();
    bool hasDeferredSubscriptionTreeNodesForPurging();
    bool purgeSubscriptionTree();
    bool hasDeferredRetainedMessageNodesForPurging();
    bool expireRetainedMessages();

    int64_t getRetainedMessageCount() const;
    uint64_t getSessionCount() const;
    int64_t getSubscriptionCount();

    void saveRetainedMessages(const std::string &filePath, bool sleep_after_limit);
    void loadRetainedMessages(const std::string &filePath);

    void saveSessionsAndSubscriptions(const std::string &filePath);
    void loadSessionsAndSubscriptions(const std::string &filePath);

    void queueSessionRemoval(const std::shared_ptr<Session> &session);
};

#endif // SUBSCRIPTIONSTORE_H
