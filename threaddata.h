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
#include <shared_mutex>
#include <functional>
#include <chrono>

#include "forward_declarations.h"

#include "client.h"
#include "subscriptionstore.h"
#include "utils.h"
#include "configfileparser.h"
#include "authplugin.h"
#include "logger.h"

typedef void (*thread_f)(ThreadData *);

class ThreadData
{
    std::unordered_map<int, std::shared_ptr<Client>> clients_by_fd;
    std::mutex clients_by_fd_mutex;
    std::shared_ptr<SubscriptionStore> subscriptionStore;
    Logger *logger;

    uint64_t receivedMessageCount = 0;
    uint64_t receivedMessageCountPrevious = 0;
    std::chrono::time_point<std::chrono::steady_clock> receivedMessagePreviousTime = std::chrono::steady_clock::now();

    uint64_t sentMessageCount = 0;
    uint64_t sentMessageCountPrevious = 0;
    std::chrono::time_point<std::chrono::steady_clock> sentMessagePreviousTime = std::chrono::steady_clock::now();


    void reload(std::shared_ptr<Settings> settings);
    void wakeUpThread();
    void doKeepAliveCheck();
    void quit();
    void publishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads);
    void publishStat(const std::string &topic, uint64_t n);

public:
    Settings settingsLocalCopy; // Is updated on reload, within the thread loop.
    Authentication authentication;
    bool running = true;
    bool finished = false;
    std::thread thread;
    int threadnr = 0;
    int epollfd = 0;
    int taskEventFd = 0;
    std::mutex taskQueueMutex;
    std::forward_list<std::function<void()>> taskQueue;

    ThreadData(int threadnr, std::shared_ptr<SubscriptionStore> &subscriptionStore, std::shared_ptr<Settings> settings);
    ThreadData(const ThreadData &other) = delete;
    ThreadData(ThreadData &&other) = delete;

    void start(thread_f f);

    void giveClient(std::shared_ptr<Client> client);
    std::shared_ptr<Client> getClient(int fd);
    void removeClient(std::shared_ptr<Client> client);
    void removeClient(int fd);
    std::shared_ptr<SubscriptionStore> &getSubscriptionStore();

    void initAuthPlugin();
    void cleanupAuthPlugin();
    void queueReload(std::shared_ptr<Settings> settings);
    void queueDoKeepAliveCheck();
    void queueQuit();
    void waitForQuit();
    void queuePasswdFileReload();
    void queuePublishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads);

    int getNrOfClients() const;

    void incrementReceivedMessageCount();
    uint64_t getReceivedMessageCount() const;
    uint64_t getReceivedMessagePerSecond();

    void incrementSentMessageCount(uint64_t n);
    uint64_t getSentMessageCount() const;
    uint64_t getSentMessagePerSecond();

    void queueAuthPluginPeriodicEvent();
    void authPluginPeriodicEvent();
};

#endif // THREADDATA_H
