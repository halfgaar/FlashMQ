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

#include "forward_declarations.h"

#include "client.h"
#include "utils.h"
#include "configfileparser.h"
#include "plugin.h"
#include "logger.h"
#include "derivablecounter.h"
#include "queuedtasks.h"

typedef void (*thread_f)(ThreadData *);

struct KeepAliveCheck
{
    std::weak_ptr<Client> client;
    bool recheck = true;

    KeepAliveCheck(const std::shared_ptr<Client> client);
};

struct AsyncAuth
{
    std::weak_ptr<Client> client;
    AuthResult result;
    std::string authMethod;
    std::string authData;

public:
    AsyncAuth(std::weak_ptr<Client> client, AuthResult result, const std::string authMethod, const std::string &authData);
};

class ThreadData
{
    std::unordered_map<int, std::shared_ptr<Client>> clients_by_fd;
    std::mutex clients_by_fd_mutex;
    Logger *logger;

    std::mutex clientsToRemoveMutex;
    std::forward_list<std::weak_ptr<Client>> clientsQueuedForRemoving;

    std::mutex asyncClientsReadyMutex;
    std::forward_list<AsyncAuth> asyncClientsReady;

    std::mutex queuedKeepAliveMutex;
    std::map<std::chrono::seconds, std::vector<KeepAliveCheck>> queuedKeepAliveChecks;

    const PluginLoader &pluginLoader;

    void reload(const Settings &settings);
    void wakeUpThread();
    void doKeepAliveCheck();
    void quit();
    void publishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads);
    void publishStat(const std::string &topic, uint64_t n);
    void sendQueuedWills();
    void removeExpiredSessions();
    void removeExpiredRetainedMessages();
    void sendAllWills();
    void sendAllDisconnects();
    void queueClientNextKeepAliveCheck(std::shared_ptr<Client> &client, bool keepRechecking);
    void continueAsyncAuths();
    void clientDisconnectEvent(const std::string &clientid);

    void removeQueuedClients();

public:
    Settings settingsLocalCopy; // Is updated on reload, within the thread loop.
    Authentication authentication;
    bool running = true;
    bool finished = false;
    bool allWillsQueued = false;
    bool allDisconnectsSent = false;
    std::thread thread;
    int threadnr = 0;
    int epollfd = 0;
    int taskEventFd = 0;
    std::mutex taskQueueMutex;
    std::list<std::function<void()>> taskQueue;
    QueuedTasks delayedTasks;
    std::unordered_map<int, std::weak_ptr<void>> externalFds;

    DerivableCounter receivedMessageCounter;
    DerivableCounter sentMessageCounter;
    DerivableCounter mqttConnectCounter;

    ThreadData(int threadnr, const Settings &settings, const PluginLoader &pluginLoader);
    ThreadData(const ThreadData &other) = delete;
    ThreadData(ThreadData &&other) = delete;

    void start(thread_f f);

    void giveClient(std::shared_ptr<Client> client);
    std::shared_ptr<Client> getClient(int fd);
    void removeClientQueued(const std::shared_ptr<Client> &client);
    void removeClientQueued(int fd);
    void removeClient(std::shared_ptr<Client> client);

    void initplugin();
    void cleanupplugin();
    void queueReload(const Settings &settings);
    void queueDoKeepAliveCheck();
    void queueQuit();
    void waitForQuit();
    void queuePasswdFileReload();
    void queuePublishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads);
    void queueSendingQueuedWills();
    void queueRemoveExpiredSessions();
    void queueRemoveExpiredRetainedMessages();
    void queueClientNextKeepAliveCheckLocked(std::shared_ptr<Client> &client, bool keepRechecking);
    void continuationOfAuthentication(std::shared_ptr<Client> &client, AuthResult authResult, const std::string &authMethod, const std::string &returnData);
    void queueContinuationOfAuthentication(const std::shared_ptr<Client> &client, AuthResult authResult, const std::string &authMethod, const std::string &returnData);
    void queueClientDisconnectEvent(const std::string &clientid);

    int getNrOfClients() const;

    void queuepluginPeriodicEvent();
    void pluginPeriodicEvent();

    void queueSendWills();
    void queueSendDisconnects();

    void pollExternalFd(int fd, uint32_t events, const std::weak_ptr<void> &p);
    void pollExternalRemove(int fd);
    uint32_t addTask(std::function<void()> f, uint32_t delayMs);
    void removeTask(uint32_t id);
};

#endif // THREADDATA_H
