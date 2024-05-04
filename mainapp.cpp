/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "mainapp.h"
#include <cassert>
#include "exceptions.h"
#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <arpa/inet.h>
#include <memory>
#include <malloc.h>
#include <netinet/tcp.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "logger.h"
#include "threadglobals.h"
#include "threadloop.h"
#include "plugin.h"
#include "threadglobals.h"
#include "globalstats.h"
#include "utils.h"
#include "bridgeconfig.h"
#include "bridgeinfodb.h"

MainApp *MainApp::instance = nullptr;

MainApp::MainApp(const std::string &configFilePath) :
    subscriptionStore(std::make_shared<SubscriptionStore>())
{
    epollFdAccept = check<std::runtime_error>(epoll_create(999));
    taskEventFd = eventfd(0, EFD_NONBLOCK);

    confFileParser = std::make_unique<ConfigFileParser>(configFilePath);
    loadConfig(false);

    this->num_threads = get_nprocs();
    if (settings.threadCount > 0)
    {
        this->num_threads = settings.threadCount;
        logger->logf(LOG_NOTICE, "%d threads specified by 'thread_count'.", num_threads);
    }
    else
    {
        logger->logf(LOG_NOTICE, "%d CPUs are detected, making as many threads. Use 'thread_count' setting to override.", num_threads);
    }

    if (num_threads <= 0)
        throw std::runtime_error("Invalid number of CPUs: " + std::to_string(num_threads));

    {
        auto f = std::bind(&MainApp::queueCleanup, this);
        //const uint64_t derrivedSessionCheckInterval = std::max<uint64_t>((settings.expireSessionsAfterSeconds)*1000*2, 600000);
        //const uint64_t sessionCheckInterval = std::min<uint64_t>(derrivedSessionCheckInterval, 86400000);
        uint64_t interval = 10000;
#ifdef TESTING
        interval = 1000;
#endif
        timer.addCallback(f, interval, "session expiration");
    }

    {
        uint64_t interval = 1846849; // prime
        auto f = std::bind(&MainApp::queuePurgeSubscriptionTree, this);
        timer.addCallback(f, interval, "Rebuild subscription tree");
    }

    {
        uint64_t interval = 3949193; // prime
#ifdef TESTING
        interval = 500;
#endif
        auto f = std::bind(&MainApp::queueRetainedMessageExpiration, this);
        timer.addCallback(f, interval, "Purge expired retained messages.");
    }

    auto fKeepAlive = std::bind(&MainApp::queueKeepAliveCheckAtAllThreads, this);
    timer.addCallback(fKeepAlive, 5000, "keep-alive check");

    auto fPasswordFileReload = std::bind(&MainApp::queuePasswordFileReloadAllThreads, this);
    timer.addCallback(fPasswordFileReload, 2000, "Password file reload.");

    auto fPublishStats = std::bind(&MainApp::queuePublishStatsOnDollarTopic, this);
    timer.addCallback(fPublishStats, 10000, "Publish stats on $SYS");

    if (settings.pluginTimerPeriod > 0)
    {
        auto fpluginPeriodicEvent = std::bind(&MainApp::queuepluginPeriodicEventAllThreads, this);
        timer.addCallback(fpluginPeriodicEvent, settings.pluginTimerPeriod*1000, "Auth plugin periodic event.");
    }

    if (!settings.storageDir.empty())
    {
        const std::string retainedDbPath = settings.getRetainedMessagesDBFile();
        if (settings.retainedMessagesMode == RetainedMessagesMode::Enabled)
            subscriptionStore->loadRetainedMessages(settings.getRetainedMessagesDBFile());
        else
            logger->logf(LOG_INFO, "Not loading '%s', because 'retained_messages_mode' is not 'enabled'.", retainedDbPath.c_str());

        subscriptionStore->loadSessionsAndSubscriptions(settings.getSessionsDBFile());
    }

    auto fSaveState = std::bind(&MainApp::queueSaveStateInThread, this);
    timer.addCallback(fSaveState, 900000, "Save state.");

    auto fSendPendingWills = std::bind(&MainApp::queueSendQueuedWills, this);
    timer.addCallback(fSendPendingWills, 2000, "Publish pending wills.");

    auto fInternalHeartbeat = std::bind(&MainApp::queueInternalHeartbeat, this);
    timer.addCallback(fInternalHeartbeat, HEARTBEAT_INTERVAL, "Internal heartbeat.");
}

MainApp::~MainApp()
{
    if (taskEventFd >= 0)
        close(taskEventFd);

    if (epollFdAccept >= 0)
        close(epollFdAccept);
}

void MainApp::doHelp(const char *arg)
{
    puts("FlashMQ - the scalable light-weight MQTT broker");
    puts("");
    printf("Usage: %s [options]\n", arg);
    puts("");
    puts(" -h, --help                           Print help");
    puts(" -c, --config-file <flashmq.conf>     Configuration file. Default '/etc/flashmq/flashmq.conf'.");
    puts(" -t, --test-config                    Test configuration file.");
#ifndef NDEBUG
    puts(" -z, --fuzz-file <inputdata.dat>      For fuzzing, provides the bytes that would be sent by a client.");
    puts("                                      If the name contains 'web' it will activate websocket mode.");
    puts("                                      If the name also contains 'upgrade', it will assume the websocket");
    puts("                                      client is upgrade, and bypass the cryptograhically secured websocket");
    puts("                                      handshake.");
#endif
    puts(" -V, --version                        Show version");
    puts(" -l, --license                        Show license");
}

void MainApp::showLicense()
{
    std::string sse = "without SSE support";
#ifdef __SSE4_2__
    sse = "with SSE4.2 support";
#endif

    printf("FlashMQ Version %s %s\n", FLASHMQ_VERSION, sse.c_str());
    puts("Copyright (C) 2021-2024 Wiebe Cazemier.");
    puts("License OSL3: Open Software License 3.0 <https://opensource.org/license/osl-3-0-php/>.");
    puts("");
    puts("Author: Wiebe Cazemier <wiebe@flashmq.org>");
}

std::list<ScopedSocket> MainApp::createListenSocket(const std::shared_ptr<Listener> &listener)
{
    std::list<ScopedSocket> result;

    if (listener->port <= 0)
        return result;

    bool error = false;

    for (ListenerProtocol p : std::list<ListenerProtocol>({ ListenerProtocol::IPv4, ListenerProtocol::IPv6}))
    {
        std::string pname = p == ListenerProtocol::IPv4 ? "IPv4" : "IPv6";
        int family = p == ListenerProtocol::IPv4 ? AF_INET : AF_INET6;

        if (!(listener->protocol == ListenerProtocol::IPv46 || listener->protocol == p))
            continue;

        try
        {
            std::string haproxy = "";

            if (listener->isHaProxy())
                haproxy = "haproxy ";

            logger->logf(LOG_NOTICE, "Creating %s %s %slistener on [%s]:%d", pname.c_str(), listener->getProtocolName().c_str(),
                         haproxy.c_str(), listener->getBindAddress(p).c_str(), listener->port);

            BindAddr bindAddr = getBindAddr(family, listener->getBindAddress(p), listener->port);

            ScopedSocket uniqueListenFd(check<std::runtime_error>(socket(family, SOCK_STREAM, 0)), listener);

            // Not needed for now. Maybe I will make multiple accept threads later, with SO_REUSEPORT.
            int optval = 1;
            check<std::runtime_error>(setsockopt(uniqueListenFd.get(), SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval)));

            if (listener->isTcpNoDelay())
            {
                int tcp_nodelay_optval = 1;
                check<std::runtime_error>(setsockopt(uniqueListenFd.get(), IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay_optval, sizeof(tcp_nodelay_optval)));
            }

            int flags = fcntl(uniqueListenFd.get(), F_GETFL);
            check<std::runtime_error>(fcntl(uniqueListenFd.get(), F_SETFL, flags | O_NONBLOCK ));

            check<std::runtime_error>(bind(uniqueListenFd.get(), bindAddr.p.get(), bindAddr.len));
            check<std::runtime_error>(listen(uniqueListenFd.get(), 32768));

            struct epoll_event ev;
            memset(&ev, 0, sizeof (struct epoll_event));

            ev.data.fd = uniqueListenFd.get();
            ev.events = EPOLLIN;
            check<std::runtime_error>(epoll_ctl(this->epollFdAccept, EPOLL_CTL_ADD, uniqueListenFd.get(), &ev));

            result.push_back(std::move(uniqueListenFd));
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Creating %s %s listener on [%s]:%d failed: %s", pname.c_str(), listener->getProtocolName().c_str(),
                         listener->getBindAddress(p).c_str(), listener->port, ex.what());
            error = true;
        }
    }

    if (error)
        return std::list<ScopedSocket>();

    return result;
}

void MainApp::wakeUpThread()
{
    uint64_t one = 1;
    check<std::runtime_error>(write(taskEventFd, &one, sizeof(uint64_t)));
}

void MainApp::queueKeepAliveCheckAtAllThreads()
{
    for (std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueDoKeepAliveCheck();
    }
}

void MainApp::queuePasswordFileReloadAllThreads()
{
    for (std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queuePasswdFileReload();
    }
}

void MainApp::queuepluginPeriodicEventAllThreads()
{
    for (std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queuepluginPeriodicEvent();
    }
}

void MainApp::setFuzzFile(const std::string &fuzzFilePath)
{
    this->fuzzFilePath = fuzzFilePath;
}

/**
 * @brief MainApp::queuePublishStatsOnDollarTopic publishes the dollar topics, on a thread that has thread local authentication.
 */
void MainApp::queuePublishStatsOnDollarTopic()
{
    std::lock_guard<std::mutex> locker(eventMutex);

    if (!threads.empty())
    {
        auto f = std::bind(&ThreadData::queuePublishStatsOnDollarTopic, threads.front().get(), threads);
        taskQueue.push_back(f);

        wakeUpThread();
    }
}

/**
 * @brief MainApp::saveStateInThread starts a thread for disk IO, because file IO is not async.
 */
void MainApp::saveStateInThread()
{
    std::list<BridgeInfoForSerializing> bridgeInfos = BridgeInfoForSerializing::getBridgeInfosForSerializing(this->bridgeConfigs);

    auto f = std::bind(&MainApp::saveState, this->settings, bridgeInfos, true);
    this->bgWorker.addTask(f);
}

/**
 * @brief MainApp::queueSaveStateInThread is a wrapper to be called from another thread, to make sure saveStateInThread() is called
 * on the main loop.
 */
void MainApp::queueSaveStateInThread()
{
    std::lock_guard<std::mutex> locker(eventMutex);
    auto f = std::bind(&MainApp::saveStateInThread, this);
    taskQueue.push_back(f);
    wakeUpThread();
}

void MainApp::queueSendQueuedWills()
{
    if (!threads.empty())
    {
        int threadnr = rand() % threads.size();
        std::shared_ptr<ThreadData> t = threads[threadnr];
        t->queueSendingQueuedWills();
    }
}

void MainApp::waitForWillsQueued()
{
    int i = 0;

    while(std::any_of(threads.begin(), threads.end(), [](std::shared_ptr<ThreadData> t){ return !t->allWillsQueued && t->running; }) && i++ < 5000)
    {
        usleep(1000);
    }
}

void MainApp::waitForDisconnectsInitiated()
{
    int i = 0;

    while(std::any_of(threads.begin(), threads.end(), [](std::shared_ptr<ThreadData> t){ return !t->allDisconnectsSent && t->running; }) && i++ < 5000)
    {
        usleep(1000);
    }
}

void MainApp::queueRetainedMessageExpiration()
{
    if (!threads.empty())
    {
        int threadnr = rand() % threads.size();
        std::shared_ptr<ThreadData> t = threads[threadnr];
        t->queueRemoveExpiredRetainedMessages();
    }
}

void MainApp::sendBridgesToThreads()
{
    if (threads.empty())
        return;

    int i = 0;
    auto bridge_pos = this->bridgeConfigs.begin();
    while (bridge_pos != this->bridgeConfigs.end())
    {
        auto cur = bridge_pos;
        bridge_pos++;

        std::shared_ptr<BridgeConfig> bridge = cur->second;

        if (!bridge)
            continue;

        std::shared_ptr<ThreadData> owner = bridge->owner.lock();

        if (!owner)
        {
            owner = threads.at(i++ % threads.size());
            bridge->owner = owner;
        }

        if (bridge->queueForDelete)
        {
            owner->removeBridgeQueued(bridge, "Bridge disappeared from config");
            this->bridgeConfigs.erase(cur);
        }
        else
        {
            std::shared_ptr<BridgeState> bridgeState = std::make_shared<BridgeState>(*bridge);
            bridgeState->threadData = owner;
            owner->giveBridge(bridgeState);
        }
    }
}

void MainApp::queueSendBridgesToThreads()
{
    {
        std::lock_guard<std::mutex> locker(eventMutex);
        auto f = std::bind(&MainApp::sendBridgesToThreads, this);
        taskQueue.push_back(f);
    }

    wakeUpThread();
}

void MainApp::queueBridgeReconnectAllThreads(bool alsoQueueNexts)
{
    try
    {
        for (std::shared_ptr<ThreadData> &thread : threads)
        {
            thread->queueBridgeReconnect();
        }
    }
    catch (std::exception &ex)
    {
        Logger *logger = Logger::getInstance();
        logger->logf(LOG_ERR, ex.what());
    }

    if (alsoQueueNexts)
    {
        auto fReconnectBridges = std::bind(&MainApp::queueBridgeReconnectAllThreads, this, false);
        timer.addCallback(fReconnectBridges, 5000, "Reconnect bridges.");
    }
}

void MainApp::queueInternalHeartbeat()
{
    if (threads.empty())
        return;

    auto set_drift = [this](std::chrono::time_point<std::chrono::steady_clock> queue_time) {
        const std::chrono::milliseconds main_loop_drift = drift.getDrift();
        if (main_loop_drift > settings.maxEventLoopDrift)
        {
            Logger::getInstance()->log(LOG_WARNING) << "Main loop thread drift is " << main_loop_drift.count() << " ms.";
        }
        if (this->medianThreadDrift > settings.maxEventLoopDrift)
        {
            Logger::getInstance()->log(LOG_WARNING) << "Median thread drift is " << this->medianThreadDrift.count() << " ms.";
        }

        drift.update(queue_time);

        std::vector<std::chrono::milliseconds> drifts(threads.size());

        std::transform(threads.begin(), threads.end(), drifts.begin(), [] (const std::shared_ptr<const ThreadData> &t) {
            return t->driftCounter.getDrift();
        });

        const size_t n = drifts.size() / 2;
        std::nth_element(drifts.begin(), drifts.begin() + n, drifts.end());
        this->medianThreadDrift = drifts.at(n);
    };

    {
        auto call_set_drift = std::bind(set_drift, std::chrono::steady_clock::now());
        std::lock_guard<std::mutex> locker(eventMutex);
        taskQueue.push_back(call_set_drift);
    }

    wakeUpThread();

    for (std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueInternalHeartbeat();
    }
}

/**
 * @brief MainApp::saveState saves sessions and such to files. It's run in the main thread, but also dedicated threads. For that,
 * reason, it's a static method to reduce the risk of accidental use of data without locks.
 * @param settings A local settings, copied from a std::bind copy when running in a thread, because of thread safety.
 * @param bridgeInfos is a list of objects already prepared from the original bridge configs, to avoid concurrent access.
 */
void MainApp::saveState(const Settings &settings, const std::list<BridgeInfoForSerializing> &bridgeInfos, bool sleep_after_limit)
{
    Logger *logger = Logger::getInstance();

    try
    {
        if (!settings.storageDir.empty())
        {
            std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();

            const std::string retainedDBPath = settings.getRetainedMessagesDBFile();
            if (settings.retainedMessagesMode == RetainedMessagesMode::Enabled)
                subscriptionStore->saveRetainedMessages(retainedDBPath, sleep_after_limit);
            else
                logger->logf(LOG_INFO, "Not saving '%s', because 'retained_messages_mode' is not 'enabled'.", retainedDBPath.c_str());

            const std::string sessionsDBPath = settings.getSessionsDBFile();
            subscriptionStore->saveSessionsAndSubscriptions(sessionsDBPath);

            MainApp::saveBridgeInfo(settings.getBridgeNamesDBFile(), bridgeInfos);

            logger->logf(LOG_NOTICE, "Saving states done");
        }
    }
    catch(std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error saving state: %s", ex.what());
    }
}

void MainApp::saveBridgeInfo(const std::string &filePath, const std::list<BridgeInfoForSerializing> &bridgeInfos)
{
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_NOTICE, "Saving bridge info in '%s'", filePath.c_str());
    BridgeInfoDb bridgeInfoDb(filePath);
    bridgeInfoDb.openWrite();
    bridgeInfoDb.saveInfo(bridgeInfos);
}

std::list<std::shared_ptr<BridgeConfig>> MainApp::loadBridgeInfo(Settings &settings)
{
    Logger *logger = Logger::getInstance();
    std::list<std::shared_ptr<BridgeConfig>> bridges = settings.stealBridges();

    if (settings.storageDir.empty())
        return bridges;

    const std::string filePath = settings.getBridgeNamesDBFile();

    try
    {
        logger->logf(LOG_NOTICE, "Loading '%s'", filePath.c_str());

        BridgeInfoDb dbfile(filePath);
        dbfile.openRead();
        std::list<BridgeInfoForSerializing> bridgeInfos = dbfile.readInfo();

        for(const BridgeInfoForSerializing &info : bridgeInfos)
        {
            for(std::shared_ptr<BridgeConfig> &bridgeConfig : bridges)
            {
                if (!bridgeConfig->useSavedClientId)
                    continue;

                if (bridgeConfig->clientidPrefix == info.prefix)
                {
                    logger->log(LOG_INFO) << "Assigning stored bridge clientid '" << info.clientId << "' to bridge '" << info.prefix << "'.";
                    bridgeConfig->setClientId(info.prefix, info.clientId);
                    break;
                }
            }
        }
    }
    catch (PersistenceFileCantBeOpened &ex)
    {
        logger->logf(LOG_WARNING, "File '%s' is not there (yet)", filePath.c_str());
    }

    return bridges;
}

void MainApp::initMainApp(int argc, char *argv[])
{
    if (instance != nullptr)
        throw std::runtime_error("App was already initialized.");

    static struct option long_options[] =
    {
        {"help", no_argument, nullptr, 'h'},
        {"config-file", required_argument, nullptr, 'c'},
        {"test-config", no_argument, nullptr, 't'},
        {"fuzz-file", required_argument, nullptr, 'z'},
        {"version", no_argument, nullptr, 'V'},
        {"license", no_argument, nullptr, 'l'},
        {nullptr, 0, nullptr, 0}
    };

#ifdef TESTING
    const std::string defaultConfigFile = "/dummy/flashmq.org";
#else
    const std::string defaultConfigFile = "/etc/flashmq/flashmq.conf";
#endif
    std::string configFile;

    if (access(defaultConfigFile.c_str(), R_OK) == 0)
    {
        configFile = defaultConfigFile;
    }

    std::string fuzzFile;

    int option_index = 0;
    int opt;
    bool testConfig = false;
    optind = 1; // allow repeated calls to getopt_long.
    while((opt = getopt_long(argc, argv, "hc:Vltz:", long_options, &option_index)) != -1)
    {
        switch(opt)
        {
        case 'c':
            configFile = optarg;
            break;
        case 'l':
            MainApp::showLicense();
            exit(0);
        case 'V':
            MainApp::showLicense();
            exit(0);
        case 'z':
            fuzzFile = optarg;
            break;
        case 'h':
            MainApp::doHelp(argv[0]);
            exit(16);
        case 't':
            testConfig = true;
            break;
        case '?':
            MainApp::doHelp(argv[0]);
            exit(16);
        }
    }

    if (testConfig)
    {
        try
        {
            if (configFile.empty())
            {
                std::cerr << "No config specified (with -c) and the default " << defaultConfigFile << " not found." << std::endl << std::endl;
                MainApp::doHelp(argv[0]);
                exit(1);
            }

            ConfigFileParser c(configFile);
            c.loadFile(true);
            printf("Config '%s' OK\n", configFile.c_str());
            exit(0);
        }
        catch (ConfigFileException &ex)
        {
            std::cerr << ex.what() << std::endl;
            exit(1);
        }
    }

    instance = new MainApp(configFile);
    instance->setFuzzFile(fuzzFile);
}


MainApp *MainApp::getMainApp()
{
    if (!instance)
        throw std::runtime_error("You haven't initialized the app yet.");
    return instance;
}

void MainApp::start()
{
#ifndef NDEBUG
#ifndef TESTING
    if (!getFuzzMode())
    {
        oneInstanceLock.lock();
    }
#endif
#endif

#ifdef NDEBUG
    logger->noLongerLogToStd();
#endif

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = taskEventFd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(this->epollFdAccept, EPOLL_CTL_ADD, taskEventFd, &ev));

#ifndef NDEBUG
    // I fuzzed using afl-fuzz. You need to compile it with their compiler.
    if (getFuzzMode())
    {
        // No threads for execution stability/determinism.
        num_threads = 0;

        settings.allowAnonymous = true;

        int fd = open(fuzzFilePath.c_str(), O_RDONLY);
        assert(fd > 0);

        int fdnull = open("/dev/null", O_RDWR);
        assert(fdnull > 0);

        int fdnull2 = open("/dev/null", O_RDWR);
        assert(fdnull2 > 0);

        // TODO: matching for filename patterns doesn't work, because AFL fuzz changes the name.
        const std::string fuzzFilePathLower = str_tolower(fuzzFilePath);
        bool fuzzWebsockets = strContains(fuzzFilePathLower, "web");

        try
        {
            const std::string empty;

            Authentication auth(settings);
            ThreadGlobals::assign(&auth);

            std::vector<MqttPacket> packetQueueIn;
            std::vector<std::string> subtopics;

            PluginLoader pluginLoader;

            std::shared_ptr<ThreadData> threaddata = std::make_shared<ThreadData>(0, settings, pluginLoader);
            ThreadGlobals::assignThreadData(threaddata.get());

            std::shared_ptr<Client> client = std::make_shared<Client>(fd, threaddata, nullptr, fuzzWebsockets, false, nullptr, settings, true);
            std::shared_ptr<Client> subscriber = std::make_shared<Client>(fdnull, threaddata, nullptr, fuzzWebsockets, false, nullptr, settings, true);
            subscriber->setClientProperties(ProtocolVersion::Mqtt311, "subscriber", "subuser", true, 60);
            subscriber->setAuthenticated(true);

            std::shared_ptr<Client> websocketsubscriber = std::make_shared<Client>(fdnull2, threaddata, nullptr, true, false, nullptr, settings, true);
            websocketsubscriber->setClientProperties(ProtocolVersion::Mqtt311, "websocketsubscriber", "websocksubuser", true, 60);
            websocketsubscriber->setAuthenticated(true);
            websocketsubscriber->setFakeUpgraded();
            subscriptionStore->registerClientAndKickExistingOne(websocketsubscriber);
            subtopics = splitTopic("#");
            subscriptionStore->addSubscription(websocketsubscriber, subtopics, 0, false, false, empty, AuthResult::success);

            subscriptionStore->registerClientAndKickExistingOne(subscriber);
            subscriptionStore->addSubscription(subscriber, subtopics, 0, false, false, empty, AuthResult::success);

            if (fuzzWebsockets && strContains(fuzzFilePathLower, "upgrade"))
            {
                client->setFakeUpgraded();
                subscriber->setFakeUpgraded();
            }

            {
                VectorClearGuard vectorClearGuard(packetQueueIn);
                client->readFdIntoBuffer();
                client->bufferToMqttPackets(packetQueueIn, client);

                for (MqttPacket &packet : packetQueueIn)
                {
                    packet.handle();
                }

                subscriber->writeBufIntoFd();
                websocketsubscriber->writeBufIntoFd();
            }
        }
        catch (ProtocolError &ex)
        {
            logger->logf(LOG_ERR, "Expected MqttPacket handling error: %s", ex.what());
        }

        running = false;
    }
#endif

    GlobalStats *globalStats = GlobalStats::getInstance();

    PluginLoader pluginLoader;
    pluginLoader.loadPlugin(settings.pluginPath);

    std::unordered_map<std::string, std::string> &authOpts = settings.getFlashmqpluginOpts();
    pluginLoader.mainInit(authOpts);

    for (int i = 0; i < num_threads; i++)
    {
        std::shared_ptr<ThreadData> t = std::make_shared<ThreadData>(i, settings, pluginLoader);
        t->start(&do_thread_work);
        threads.push_back(t);
    }

    // Populate the $SYS topics, otherwise you have to wait until the timer expires.
    if (!threads.empty())
        threads.front()->queuePublishStatsOnDollarTopic(threads);

    timer.start();

    sendBridgesToThreads();
    queueBridgeReconnectAllThreads(true);

    uint next_thread_index = 0;

    this->bgWorker.start();

    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    started = true;
    while (running)
    {
        int num_fds = epoll_wait(this->epollFdAccept, events, MAX_EVENTS, 100);

        if (num_fds < 0)
        {
            if (errno == EINTR)
                continue;
            logger->logf(LOG_ERR, "Waiting for listening socket error: %s", strerror(errno));
        }

        for (int i = 0; i < num_fds; i++)
        {
            int cur_fd = events[i].data.fd;
            try
            {
                if (cur_fd != taskEventFd)
                {
                    std::shared_ptr<Listener> listener = activeListenSockets[cur_fd].getListener();
                    if (!listener)
                        continue;

                    std::shared_ptr<ThreadData> thread_data = threads[next_thread_index++ % num_threads];

                    logger->logf(LOG_DEBUG, "Accepting connection on thread %d on %s", thread_data->threadnr, listener->getProtocolName().c_str());

                    struct sockaddr_in6 addrBiggest;
                    struct sockaddr *addr = reinterpret_cast<sockaddr*>(&addrBiggest);
                    socklen_t len = sizeof(struct sockaddr_in6);
                    memset(addr, 0, len);
                    int fd = check<std::runtime_error>(accept(cur_fd, addr, &len));

                    /*
                     * I decided to not use a delayed close mechanism. It has been observed that under overload and clients in a reconnect loop,
                     * you can collect open files up to (a) million(s). By accepting and closing, the hope is we can keep clients at bay from
                     * the thread loops well enough.
                     */
                    if (this->medianThreadDrift > settings.maxEventLoopDrift || this->drift.getDrift() > settings.maxEventLoopDrift)
                    {
                        const std::string addr_s = sockaddrToString(addr);
                        bool do_close = false;

                        if (settings.overloadMode == OverloadMode::CloseNewClients)
                        {
                            if (overloadLogCounter <= OVERLOAD_LOGS_MUTE_AFTER_LINES)
                            {
                                overloadLogCounter++;
                                logger->log(LOG_ERROR) << "[OVERLOAD] FlashMQ seems to be overloaded while accepting new connection(s) from '"
                                                       << addr_s << ". Closing socket. See 'overload_mode' and 'max_event_loop_drift'.";
                            }
                            do_close = true;
                        }
                        else if (settings.overloadMode == OverloadMode::Log)
                        {
                            if (overloadLogCounter <= OVERLOAD_LOGS_MUTE_AFTER_LINES)
                            {
                                overloadLogCounter++;
                                logger->log(LOG_WARNING) << "[OVERLOAD] FlashMQ seems to be overloaded while accepting new connection(s) from '"
                                                         << addr_s << ". See 'overload_mode' and 'max_event_loop_drift'.";
                            }
                        }
                        else
                        {
                            throw std::runtime_error("Unimplemented OverloadMode");
                        }

                        if (overloadLogCounter > OVERLOAD_LOGS_MUTE_AFTER_LINES && overloadLogCounter < OVERLOAD_LOGS_MUTE_AFTER_LINES * 2)
                        {
                            overloadLogCounter = OVERLOAD_LOGS_MUTE_AFTER_LINES * 5;
                            logger->log(LOG_WARNING) << "[OVERLOAD] Muting overload logging until it recovers, to avoid log spam and extra load.";
                        }

                        if (do_close)
                        {
                            close(fd);
                            continue;
                        }
                    }
                    else
                    {
                        overloadLogCounter = 0;
                    }

                    SSL *clientSSL = nullptr;
                    if (listener->isSsl())
                    {
                        if (!listener->sslctx)
                        {
                            logger->log(LOG_ERR) << "Listener is SSL but SSL context is null. Application bug.";
                            close(fd);
                            continue;
                        }

                        clientSSL = SSL_new(listener->sslctx->get());

                        if (clientSSL == NULL)
                        {
                            logger->logf(LOG_ERR, "Problem creating SSL object. Closing client.");
                            close(fd);
                            continue;
                        }

                        SSL_set_fd(clientSSL, fd);
                    }

                    // Don't use std::make_shared to avoid the weak pointers keeping the control block in memory.
                    std::shared_ptr<Client> client = std::shared_ptr<Client>(new Client(fd, thread_data, clientSSL, listener->websocket, listener->isHaProxy(), addr, settings));

                    if (listener->getX509ClientVerficationMode() != X509ClientVerification::None)
                    {
                        client->setSslVerify(listener->getX509ClientVerficationMode());
                    }

                    client->setAllowAnonymousOverride(listener->allowAnonymous);

                    thread_data->giveClient(std::move(client));

                    globalStats->socketConnects.inc();
                }
                else
                {
                    uint64_t eventfd_value = 0;
                    check<std::runtime_error>(read(cur_fd, &eventfd_value, sizeof(uint64_t)));

                    if (doConfigReload)
                    {
                        reloadConfig();
                    }
                    if (doLogFileReOpen)
                    {
                        reopenLogfile();
                    }
                    if (doQuitAction)
                    {
                        quit();
                    }
                    if (doMemoryTrim)
                    {
                        memoryTrim();
                    }

                    std::list<std::function<void()>> tasks;

                    {
                        std::lock_guard<std::mutex> locker(eventMutex);
                        tasks = std::move(taskQueue);
                        taskQueue.clear();
                    }

                    for(auto &f : tasks)
                    {
                        f();
                    }
                }
            }
            catch (std::exception &ex)
            {
                logger->logf(LOG_ERR, "Problem in main thread: %s", ex.what());
            }

        }
    }

    this->bgWorker.stop();

    if (settings.willsEnabled)
    {
        logger->logf(LOG_DEBUG, "Having all client in all threads send or queue their will.");
        for(std::shared_ptr<ThreadData> &thread : threads)
        {
            thread->queueSendWills();
        }
        waitForWillsQueued();
    }

    logger->logf(LOG_DEBUG, "Having all client in all threads send a disconnect packet.");
    for(std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueSendDisconnects();
    }
    waitForDisconnectsInitiated();

    oneInstanceLock.unlock();

    logger->logf(LOG_DEBUG, "Signaling threads to finish.");
    for(std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueQuit();
    }

    logger->logf(LOG_DEBUG, "Waiting for threads to finish.");
    int count = 0;
    bool waitTimeExpired = false;
    while(std::any_of(threads.begin(), threads.end(), [](std::shared_ptr<ThreadData> t){ return !t->finished; }))
    {
        if (count++ >= 10000)
        {
            waitTimeExpired = true;
            break;
        }
        usleep(1000);
    }

    if (waitTimeExpired)
    {
        logger->logf(LOG_WARNING, "(Some) threads failed to terminate. Program will exit uncleanly. If you're using a plugin, it may not be thread-safe.");
    }
    else
    {
        for(std::shared_ptr<ThreadData> &thread : threads)
        {
            logger->logf(LOG_DEBUG, "Waiting for thread %d to join.", thread->threadnr);
            thread->waitForQuit();
        }
    }

    pluginLoader.mainDeinit(settings.getFlashmqpluginOpts());

    this->bgWorker.waitForStop();

    std::list<BridgeInfoForSerializing> bridgeInfos = BridgeInfoForSerializing::getBridgeInfosForSerializing(this->bridgeConfigs);
    saveState(this->settings, bridgeInfos, false);
}

void MainApp::queueQuit()
{
    this->doQuitAction = true;
    wakeUpThread();
}

void MainApp::quit()
{
    doQuitAction = false;

    std::lock_guard<std::mutex> guard(quitMutex);
    if (!running)
        return;

    Logger *logger = Logger::getInstance();
    logger->logf(LOG_NOTICE, "Quitting FlashMQ");
    timer.stop();
    running = false;
}

bool MainApp::getFuzzMode() const
{
    bool fuzzMode = false;
#ifndef NDEBUG
    fuzzMode = !fuzzFilePath.empty();
#endif
    return fuzzMode;
}

void MainApp::setlimits()
{
    rlim_t nofile = settings.rlimitNoFile;
    logger->log(LOG_INFO) << "Setting rlimit nofile to " << nofile;
    struct rlimit v = { nofile, nofile };
    if (setrlimit(RLIMIT_NOFILE, &v) < 0)
    {
        logger->logf(LOG_ERR, "Setting ulimit nofile failed: '%s'. This means the default is used. Note. It's also subject to systemd's 'LimitNOFILE', "
                              "which in turn is maxed to '/proc/sys/fs/nr_open', which can be set like 'sysctl fs.nr_open=15000000'", strerror(errno));
    }
}

/**
 * @brief MainApp::loadConfig is loaded on app start where you want it to crash, loaded from within try/catch on reload, to allow the program to continue.
 */
void MainApp::loadConfig(bool reload)
{
    Logger *logger = Logger::getInstance();

    logger->log(LOG_NOTICE) << std::boolalpha << "Loading config. Reload: " << reload << ".";

    // Atomic loading, first test.
    confFileParser->loadFile(true);
    confFileParser->loadFile(false);
    settings = confFileParser->getSettings();
    ThreadGlobals::assignSettings(&settings);

    if (settings.listeners.empty())
    {
        std::shared_ptr<Listener> defaultListener = std::make_shared<Listener>();

        // Kind of a quick hack to do this this way.
#ifdef TESTING
        defaultListener->port = 21883;
#endif

        defaultListener->isValid();
        settings.listeners.push_back(defaultListener);
    }

    listeners = settings.listeners;

    for (std::shared_ptr<Listener> &listener : this->listeners)
    {
        listener->isValid();
    }

    if (!getFuzzMode())
    {
        activeListenSockets.clear();

        bool listenerCreateError = false;
        for(std::shared_ptr<Listener> &listener : this->listeners)
        {
            listener->loadCertAndKeyFromConfig();
            std::list<ScopedSocket> scopedSockets = createListenSocket(listener);

            if (scopedSockets.empty())
            {
                listenerCreateError = true;
                continue;
            }

            for(ScopedSocket &s : scopedSockets)
            {
                activeListenSockets[s.get()] = std::move(s);
            }
        }

        if (listenerCreateError && !reload)
        {
            throw std::runtime_error("Some listeners failed.");
        }
    }

    {
        for (auto &pair : bridgeConfigs)
        {
            pair.second->queueForDelete = true;
        }

        std::list<std::shared_ptr<BridgeConfig>> bridges = loadBridgeInfo(this->settings);

        for (std::shared_ptr<BridgeConfig> &bridge : bridges)
        {
            if (!bridge)
                continue;

            auto pos = this->bridgeConfigs.find(bridge->clientidPrefix);
            if (pos != this->bridgeConfigs.end())
            {
                logger->log(LOG_NOTICE) << "Assing new config to bridge '" << bridge->clientidPrefix << "' and reconnect if needed.";

                std::shared_ptr<BridgeConfig> &cur = pos->second;

                if (!cur)
                    continue;

                std::shared_ptr<ThreadData> owner = cur->owner.lock();
                std::string clientid = cur->getClientid();
                cur = bridge;
                cur->owner = owner;
                cur->setClientId(cur->clientidPrefix, clientid);
            }
            else
            {
                logger->log(LOG_NOTICE) << "Adding bridge '" << bridge->clientidPrefix << "'.";
                this->bridgeConfigs[bridge->clientidPrefix] = bridge;
            }
        }

        for (auto &pair : bridgeConfigs)
        {
            if (pair.second->queueForDelete)
            {
                logger->log(LOG_NOTICE) << "Queueing bridge '" << pair.first << "' for removal, because it disappeared from config.";
            }
        }

        // On first load, the start() function will take care of it.
        if (reload)
        {
            sendBridgesToThreads();
            queueBridgeReconnectAllThreads(false);
        }
    }

    logger->setLogPath(settings.logPath);
    logger->queueReOpen();
    logger->setFlags(settings.logLevel, settings.logSubscriptions);
    logger->setFlags(settings.logDebug, settings.quiet);

    setlimits();

    for (std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueReload(settings);
    }
}

void MainApp::reloadConfig()
{
    doConfigReload = false;
    Logger *logger = Logger::getInstance();

    try
    {
        loadConfig(true);
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error reloading config: %s", ex.what());
    }

}

void MainApp::reopenLogfile()
{
    doLogFileReOpen = false;
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_NOTICE, "Reopening log files");
    logger->queueReOpen();
    logger->logf(LOG_NOTICE, "Log files reopened");
}

/**
 * @brief MainApp::queueConfigReload is called by a signal handler, and it was observed that it should not do anything that allocates memory,
 * to avoid locking itself when another signal is received.
 */
void MainApp::queueConfigReload()
{
    doConfigReload = true;
    wakeUpThread();
}

void MainApp::queueReopenLogFile()
{
    doLogFileReOpen = true;
    wakeUpThread();
}

void MainApp::queueCleanup()
{
    if (!threads.empty())
    {
        int threadnr = rand() % threads.size();
        std::shared_ptr<ThreadData> t = threads[threadnr];
        t->queueRemoveExpiredSessions();
    }
}

void MainApp::queuePurgeSubscriptionTree()
{
    if (!threads.empty())
    {
        int threadnr = rand() % threads.size();
        std::shared_ptr<ThreadData> t = threads[threadnr];
        t->queuePurgeSubscriptionTree();
    }
}

void MainApp::queueMemoryTrim()
{
    doMemoryTrim = true;
    wakeUpThread();
}

void MainApp::memoryTrim()
{
    doMemoryTrim = false;
    Logger *logger = Logger::getInstance();

    logger->log(LOG_NOTICE) << "Initiating malloc_trim(0). Main thread will not be able to accept new connections while it's running.";

    const auto a = std::chrono::steady_clock::now();
    const int result = malloc_trim(0);
    const auto b = std::chrono::steady_clock::now();
    const std::chrono::microseconds dur = std::chrono::duration_cast<std::chrono::microseconds>(b - a);

    std::string sresult = "Unknown result from malloc_trim(0).";
    if (result == 0)
        sresult = "Result was 0, so no memory was returned to the system.";
    else if (result == 1)
        sresult = "Result was 1, so memory was returned to the system.";

    logger->log(LOG_NOTICE) << "Operation malloc_trim(0) done. " << sresult << " Duration was " << dur.count() << " µs.";
}

std::shared_ptr<SubscriptionStore> MainApp::getSubscriptionStore()
{
    return this->subscriptionStore;
}

