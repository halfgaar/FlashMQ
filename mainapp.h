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
#include "timer.h"
#include "scopedsocket.h"
#include "oneinstancelock.h"
#include "bridgeinfodb.h"
#include "backgroundworker.h"

class MainApp
{
#ifdef TESTING
    friend class MainAppInThread;
#endif

    static MainApp *instance;
    int num_threads = 0;

    bool started = false;
    bool running = true;
    std::vector<std::shared_ptr<ThreadData>> threads;
    std::shared_ptr<SubscriptionStore> subscriptionStore;
    std::unique_ptr<ConfigFileParser> confFileParser;
    std::list<std::function<void()>> taskQueue;
    int epollFdAccept = -1;
    int taskEventFd = -1;
    bool doConfigReload = false;
    bool doLogFileReOpen = false;
    bool doQuitAction = false;
    bool doMemoryTrim = false;
    std::mutex eventMutex;
    Timer timer;

    Settings settings;

    std::list<std::shared_ptr<Listener>> listeners;
    std::unordered_map<int, ScopedSocket> activeListenSockets;

    std::list<std::shared_ptr<BridgeConfig>> bridges;
    std::mutex quitMutex;
    std::string fuzzFilePath;
    OneInstanceLock oneInstanceLock;

    Logger *logger = Logger::getInstance();

    BackgroundWorker bgWorker;

    bool getFuzzMode() const;
    void setlimits();
    void loadConfig(bool reload);
    void reloadConfig();
    void reopenLogfile();
    static void doHelp(const char *arg);
    static void showLicense();
    std::list<ScopedSocket> createListenSocket(const std::shared_ptr<Listener> &listener);
    void wakeUpThread();
    void queueKeepAliveCheckAtAllThreads();
    void queuePasswordFileReloadAllThreads();
    void queuepluginPeriodicEventAllThreads();
    void setFuzzFile(const std::string &fuzzFilePath);
    void queuePublishStatsOnDollarTopic();
    static void saveState(const Settings &settings, const std::list<BridgeInfoForSerializing> &bridgeInfos, bool sleep_after_limit);
    static void saveBridgeInfo(const std::string &filePath, const std::list<BridgeInfoForSerializing> &bridgeInfos);
    void loadBridgeInfo();
    void saveStateInThread();
    void queueSaveStateInThread();
    void queueSendQueuedWills();
    void waitForWillsQueued();
    void waitForDisconnectsInitiated();
    void queueRetainedMessageExpiration();
    void createBridge(std::shared_ptr<ThreadData> &thread, const std::shared_ptr<BridgeConfig> &bridgeConfig);
    void queueBridgeReconnectAllThreads(bool alsoQueueNexts);

    MainApp(const std::string &configFilePath);
public:
    MainApp(const MainApp &rhs) = delete;
    MainApp(MainApp &&rhs) = delete;
    ~MainApp();
    static MainApp *getMainApp();
    static void initMainApp(int argc, char *argv[]);
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

    std::shared_ptr<SubscriptionStore> getSubscriptionStore();
};

#endif // MAINAPP_H
