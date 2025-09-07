/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef THREADDATA_H
#define THREADDATA_H

#include <thread>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>
#include <forward_list>
#include <random>
#include <atomic>

#include "forward_declarations.h"
#include "plugin.h"
#include "logger.h"
#include "derivablecounter.h"
#include "queuedtasks.h"
#include "settings.h"
#include "bridgeconfig.h"
#include "driftcounter.h"
#include "fdmanaged.h"
#include "mutexowned.h"
#include "clientacceptqueue.h"

class MainApp;

void logStringStreamQueuedHelper(std::ostringstream &oss, Client *client);

struct KeepAliveCheck
{
    std::weak_ptr<Client> client;
    bool recheck = true;

    KeepAliveCheck(const std::shared_ptr<Client> client);
};

struct QueuedRetainedMessage
{
    const Publish p;
    const std::vector<std::string> subtopics;
    const std::chrono::time_point<std::chrono::steady_clock> limit;

    QueuedRetainedMessage(const Publish &p, const std::vector<std::string> &subtopics, const std::chrono::time_point<std::chrono::steady_clock> limit);
};

struct Clients
{
    std::unordered_map<int, std::shared_ptr<Client>> by_fd;
    std::unordered_map<std::string, std::shared_ptr<BridgeState>> bridges;
};

struct ThreadDataOwner
{
    const int threadnr = -1;
    std::shared_ptr<ThreadData> td;
    std::weak_ptr<ThreadData> td_weak;
    std::thread thread;

    ThreadDataOwner() = delete;
    ThreadDataOwner(const ThreadDataOwner &other) = delete;
    ThreadDataOwner(ThreadDataOwner &&other) = default;
    ThreadDataOwner(int threadnr, const Settings &settings, const std::shared_ptr<PluginLoader> &pluginLoader, const std::weak_ptr<MainApp> mainApp);
    ~ThreadDataOwner();

    ThreadDataOwner &operator=(const ThreadDataOwner &other) = delete;
    ThreadDataOwner &operator=(ThreadDataOwner &&other) = delete;

    void start();
    void waitForQuit();
    std::shared_ptr<ThreadData> getThreadData() const;
    bool running() const;
    bool finished() const;
    bool notAllWillQueuedButStillRunning() const;
    std::chrono::milliseconds getDrift() const;

    template<typename F, typename... Args>
    void callIfThread(F&& f, Args&&... args)
    {
        auto t = td_weak.lock();
        if (!t)
            return;

        return std::invoke(
            std::forward<F>(f),
            *t,
            std::forward<Args>(args)...);
    }

    void setThreadPrefix(const std::string prefix);

};

class ThreadData
{
public:

    /**
     * @brief Allows for single-shot future-proof deletion of all we own, including any shared pointers that we
     * want to clear up at the end of this thread, before our destructor runs.
     */
    struct PrivateData
    {
        Clients clients;
        std::forward_list<std::weak_ptr<Client>> clientsQueuedForRemoving;
        std::map<std::chrono::seconds, std::vector<KeepAliveCheck>> queuedKeepAliveChecks;
        std::list<QueuedRetainedMessage> queuedRetainedMessages;
    };

    /**
     * @brief Same note as PrivateData.
     */
    struct PublicData
    {
        MutexOwned<std::list<std::function<void()>>> taskQueue;
        QueuedTasks delayedTasks;
        std::unordered_map<int, std::weak_ptr<void>> externalFds;
        std::vector<std::weak_ptr<Client>> disconnectingClients;
        ClientAcceptQueue acceptQueue;
    };

private:
    FdManaged epollfd;
    std::optional<PrivateData> priv = std::make_optional<PrivateData>();
    Logger *logger;
    const std::shared_ptr<const PluginLoader> pluginLoader;
    std::string threadPrefix;

    void reload(const Settings &settings);
    void wakeUpThread();
    void doKeepAliveCheck();
    void quit();
    void publishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads);
    void publishStat(const std::string &topic, int64_t n);
    void sendQueuedWills();
    void removeExpiredSessions();
    void purgeSubscriptionTree();
    void removeExpiredRetainedMessages();
    void sendAllWills();
    void sendAllDisconnects();
    void clientDisconnectEvent(const std::string &clientid);
    void clientDisconnectActions(
            bool authenticated, const std::string &clientid, std::shared_ptr<WillPublish> &willPublish, std::shared_ptr<Session> &session,
            std::weak_ptr<BridgeState> &bridgeState, const std::string &disconnect_reason);
    void bridgeReconnect();

    void removeQueuedClients();
    void publishWithAcl(Publish &pub, bool setRetain=false);
    void removeBridge(const BridgeConfig &bridgeConfig, const std::string &reason);

public:
    std::optional<PublicData> pub = std::make_optional<PublicData>();
    Settings settingsLocalCopy; // Is updated on reload, within the thread loop.
    Authentication authentication;
    bool deferThreadReady = false;
    bool running = true;
    bool finished = false;
    bool allWillsQueued = false;
    pthread_t thread_id = pthread_self(); // Gets set later, but this helps asserts dummy use in fuzzing and tests.
    int threadnr = 0;
    int taskEventFd = -1;
    int disconnectingAllEventFd = -1;
    std::atomic<size_t> clientCount{0};
    DriftCounter driftCounter;
    std::weak_ptr<MainApp> mMainApp;

    DerivableCounter receivedMessageCounter;
    DerivableCounter sentMessageCounter;
    DerivableCounter mqttConnectCounter;
    DerivableCounter aclReadChecks;
    DerivableCounter aclWriteChecks;
    DerivableCounter aclSubscribeChecks;
    DerivableCounter aclRegisterWillChecks;
    DerivableCounter deferredRetainedMessagesSet;
    DerivableCounter deferredRetainedMessagesSetTimeout;
    DerivableCounter retainedMessageSet;

    std::minstd_rand randomish;

    ThreadData(int threadnr, const Settings &settings, const std::shared_ptr<PluginLoader> &pluginLoader, const std::weak_ptr<MainApp> mainApp);
    ThreadData(const ThreadData &other) = delete;
    ThreadData(ThreadData &&other) = delete;
    ~ThreadData();

    void setName();
    void setThreadPrefix(const std::string &prefix);
    int getEpollFd() const { return epollfd.get(); }

    void giveClient(std::shared_ptr<Client> &&client);
    void giveBridge(std::shared_ptr<BridgeState> &bridgeState);
    void removeBridgeQueued(const BridgeConfig &bridgeConfig, const std::string &reason);
    std::shared_ptr<Client> getClient(int fd);
    void removeClientQueued(const std::shared_ptr<Client> &client);
    void removeClientQueued(int fd, const Client *client, const std::string &reason);
    void removeClient(std::shared_ptr<Client> client);
    void serverInitiatedDisconnect(std::shared_ptr<Client> &&client, ReasonCodes reason, const std::string &reason_text);
    void serverInitiatedDisconnect(const std::shared_ptr<Client> &client, ReasonCodes reason, const std::string &reason_text);

    void initplugin();
    void cleanupplugin();
    void queueReload(const Settings &settings);
    void queueDoKeepAliveCheck();
    void queueQuit();
    void queuePasswdFileReload();
    void queuePublishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads);
    void queueSendingQueuedWills();
    void queueRemoveExpiredSessions();
    void queuePurgeSubscriptionTree();
    void queueRemoveExpiredRetainedMessages();

    void queueClientNextKeepAliveCheck(std::shared_ptr<Client> &client, bool keepRechecking);
    void continuationOfAuthentication(std::shared_ptr<Client> &client, AuthResult authResult, const std::string &authMethod, const std::string &returnData);
    void queueContinuationOfAuthentication(
            const std::shared_ptr<Client> &client, AuthResult authResult, const std::string &authMethod, const std::string &returnData, const uint32_t delay_in_ms);
    void queueClientDisconnectActions(
            bool authenticated, const std::string &clientid, std::shared_ptr<WillPublish> &&willPublish, std::shared_ptr<Session> &&session,
            std::weak_ptr<BridgeState> &&bridgeState, const std::string &disconnect_reason);
    void queueBridgeReconnect();
    void publishBridgeState(std::shared_ptr<BridgeState> bridge, bool connected, const std::optional<std::string> &error);
    void queueSettingRetainedMessage(const Publish &p, const std::vector<std::string> &subtopics, const std::chrono::time_point<std::chrono::steady_clock> limit);
    void setQueuedRetainedMessages();
    bool queuedRetainedMessagesEmpty() const;
    void clearQueuedRetainedMessages();
    void acceptPendingClients();
    void acceptPendingBridges();
    void deleteClients();
    void clear();

    void queuePurgeStaleTrackedLazySubscriptionsAll(const PurgeTrackedSubscriptionModifier modifier);
    void queuePurgeStaleTrackedLazySubscriptions(const std::shared_ptr<BridgeState> &bridgeState, const PurgeTrackedSubscriptionModifier modifier);
    void retryProcessingTrackedSubscriptionMutationsAll(ProcessTrackedSubscriptionMutationsModifier modifier);
    void queueProcessTrackedSubscriptionMutations(const std::shared_ptr<BridgeState> &bridgeState, const ProcessTrackedSubscriptionMutationsModifier modifier);

    size_t getNrOfClients();
    void updateNrOfClients();

    void queuepluginPeriodicEvent();
    void pluginPeriodicEvent();

    void queueSendWills();
    void queueSendDisconnects();
    void queueInternalHeartbeat();

    void pollExternalFd(int fd, uint32_t events, const std::weak_ptr<void> &p);
    void pollExternalRemove(int fd);
    uint32_t addDelayedTask(std::function<void()> f, uint32_t delayMs);
    void removeDelayedTask(uint32_t id);

    void addImmediateTask(std::function<void()> f);
    void performAllImmediateTasks();

    template<typename... Args>
    void logStringQueued(int fd, const Client *client, int level, Args... args)
    {
        auto f = [this, fd, client, level, args...] {
            std::shared_ptr<Client> clientFound;

            auto client_it = priv->clients.by_fd.find(fd);
            if (client_it != priv->clients.by_fd.end())
                clientFound = client_it->second;

            // Raw client pointer only for checking; don't dereference.
            if (!clientFound || clientFound.get() != client)
                return;

            std::ostringstream oss;

            auto handle = [&](auto arg)
            {
                if constexpr (std::is_placeholder_v<decltype(arg)> > 0)
                    logStringStreamQueuedHelper(oss, clientFound.get());
                else
                    oss << arg;
            };

            (handle(args), ...);

            Logger::getInstance()->log(level) << oss.str();
        };

        addImmediateTask(f);
    }
};



#endif // THREADDATA_H
