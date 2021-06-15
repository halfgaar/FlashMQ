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

#include "forward_declarations.h"

#include "utils.h"
#include "threaddata.h"
#include "client.h"
#include "mqttpacket.h"
#include "subscriptionstore.h"
#include "configfileparser.h"
#include "timer.h"
#include "scopedsocket.h"
#include "oneinstancelock.h"

#define VERSION "0.8.1"

class MainApp
{
    static MainApp *instance;
    int num_threads = 0;

    bool started = false;
    bool running = true;
    std::vector<std::shared_ptr<ThreadData>> threads;
    std::shared_ptr<SubscriptionStore> subscriptionStore;
    std::unique_ptr<ConfigFileParser> confFileParser;
    std::forward_list<std::function<void()>> taskQueue;
    int epollFdAccept = -1;
    int taskEventFd = -1;
    std::mutex eventMutex;
    Timer timer;
    std::shared_ptr<Settings> settings;
    std::list<std::shared_ptr<Listener>> listeners;
    std::mutex quitMutex;
    std::string fuzzFilePath;
    OneInstanceLock oneInstanceLock;

    Logger *logger = Logger::getInstance();

    void setlimits();
    void loadConfig();
    void reloadConfig();
    static void doHelp(const char *arg);
    static void showLicense();
    std::list<ScopedSocket> createListenSocket(const std::shared_ptr<Listener> &listener);
    void wakeUpThread();
    void queueKeepAliveCheckAtAllThreads();
    void queuePasswordFileReloadAllThreads();
    void queueAuthPluginPeriodicEventAllThreads();
    void setFuzzFile(const std::string &fuzzFilePath);
    void publishStatsOnDollarTopic();
    void publishStat(const std::string &topic, uint64_t n);

    MainApp(const std::string &configFilePath);
public:
    MainApp(const MainApp &rhs) = delete;
    MainApp(MainApp &&rhs) = delete;
    ~MainApp();
    static MainApp *getMainApp();
    static void initMainApp(int argc, char *argv[]);
    void start();
    void quit();
    bool getStarted() const {return started;}
    static void testConfig();

    void queueConfigReload();
    void queueCleanup();
};

#endif // MAINAPP_H
