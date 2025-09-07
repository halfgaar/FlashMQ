/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef MAINAPP_H
#define MAINAPP_H

#include <iostream>
#include <sys/socket.h>
#include <stdexcept>
#include <netinet/in.h>
#include <fcntl.h>
#include <thread>
#include <vector>
#include <functional>
#include <forward_list>
#include <list>
#include <sys/resource.h>

#include "threaddata.h"
#include "subscriptionstore.h"
#include "configfileparser.h"
#include "scopedsocket.h"
#include "oneinstancelock.h"
#include "bridgeinfodb.h"
#include "backgroundworker.h"
#include "driftcounter.h"
#include "globals.h"

class MainApp
{
#ifdef TESTING
    friend class MainAppInThread;
    friend class MainAppAsFork;
#endif

    std::weak_ptr<MainApp> mSelf;
    int num_threads = 0;

    bool started = false;
    bool running = true;
    std::vector<ThreadDataOwner> threads;
    std::shared_ptr<SubscriptionStore> subscriptionStore;
    std::unique_ptr<ConfigFileParser> confFileParser;
    int epollFdAccept = -1;
    int taskEventFd = -1;
    bool doConfigReload = false;
    bool doLogFileReOpen = false;
    bool doQuitAction = false;
    bool doMemoryTrim = false;
    QueuedTasks timed_tasks;

    uint overloadLogCounter = 0;
    DriftCounter drift;
    std::chrono::milliseconds medianThreadDrift = std::chrono::milliseconds(0);

    Settings settings;

    std::list<std::shared_ptr<Listener>> listeners;
    std::unordered_map<int, ScopedSocket> activeListenSockets;

    std::unordered_map<std::string, BridgeConfig> bridgeConfigs;
    std::mutex quitMutex;
    std::string fuzzFilePath;
    OneInstanceLock oneInstanceLock;
    const pthread_t mInitializedByThread = pthread_self();

    Logger *logger = Logger::getInstance();

    BackgroundWorker bgWorker;

    void loadPersistance();
    void setSelf(const std::shared_ptr<MainApp> &self);
    bool getFuzzMode() const;
    void setlimits();
    void loadConfig(bool reload);
    void reloadConfig();
    void reopenLogfile();
    void reloadTimers(bool reload, const Settings &old_settings);
    static void doHelp(const char *arg);
    static void showLicense();
    std::list<ScopedSocket> createListenSocket(const std::shared_ptr<Listener> &listener);
    void wakeUpThread();
    void queueKeepAliveCheckAtAllThreads();
    void queuePasswordFileReloadAllThreads();
    void queuepluginPeriodicEventAllThreads();
    void setFuzzFile(const std::string &fuzzFilePath);
    void queuePublishStatsOnDollarTopic();
    void saveStateInThread();
    void queueSendQueuedWills();
    void waitForWillsQueued();
    void queueRetainedMessageExpiration();
    void queuePurgeStaleTrackedLazySubscriptions();
    void sendBridgesToThreads();
    void queueBridgeReconnectAllThreads(bool alsoQueueNexts);
    void queueInternalHeartbeat();

    MainApp(const std::string &configFilePath);
public:
    MainApp(const MainApp &rhs) = delete;
    MainApp(MainApp &&rhs) = delete;
    ~MainApp();
    static std::shared_ptr<MainApp> initMainApp(int argc, char *argv[]);
    void start();
    void queueQuit();
    void quit();
    bool getStarted() const {return started;}
    static void testConfig();

    void queueConfigReload();
    void queueReopenLogFile();
    void queueCleanup();
    void queuePurgeSubscriptionTree();
    void queueMemoryTrim();
    void memoryTrim();
};

#endif // MAINAPP_H
