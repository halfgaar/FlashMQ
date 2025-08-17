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
#include "threadglobals.h"
#include "globalstats.h"
#include "utils.h"
#include "bridgeconfig.h"
#include "bridgeinfodb.h"
#include "globals.h"
#include "fmqssl.h"

MainApp *MainApp::instance = nullptr;

MainApp::MainApp(const std::string &configFilePath)
{
    globals = Globals();
    subscriptionStore = globals->subscriptionStore;

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

    if (!settings.storageDir.empty())
    {
        const std::string retainedDbPath = settings.getRetainedMessagesDBFile();
        if (settings.retainedMessagesMode == RetainedMessagesMode::Enabled)
            subscriptionStore->loadRetainedMessages(settings.getRetainedMessagesDBFile());
        else
            logger->logf(LOG_INFO, "Not loading '%s', because 'retained_messages_mode' is not 'enabled'.", retainedDbPath.c_str());

        subscriptionStore->loadSessionsAndSubscriptions(settings.getSessionsDBFile());
    }
}

MainApp::~MainApp()
{
    if (taskEventFd >= 0)
        close(taskEventFd);

    if (epollFdAccept >= 0)
        close(epollFdAccept);

    globals = Globals();
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
    puts("Copyright (C) 2021-2025 Wiebe Cazemier.");
    puts("License OSL3: Open Software License 3.0 <https://opensource.org/license/osl-3-0-php/>.");
    puts("");
    puts("Author: Wiebe Cazemier <wiebe@flashmq.org>");
}

std::list<ScopedSocket> MainApp::createListenSocket(const std::shared_ptr<Listener> &listener)
{
    std::list<ScopedSocket> result;

    if (listener->protocol != ListenerProtocol::Unix && listener->port <= 0)
        return result;

    std::vector<ListenerProtocol> protocols;

    if (listener->protocol == ListenerProtocol::IPv46)
    {
        protocols.push_back(ListenerProtocol::IPv4);
        protocols.push_back(ListenerProtocol::IPv6);
    }
    else
    {
        protocols.push_back(listener->protocol);
    }

    bool error = false;

    for (ListenerProtocol p : protocols)
    {
        std::string pname;
        sa_family_t family = AF_UNSPEC;

        if (p == ListenerProtocol::IPv4)
        {
            pname = "IPv4";
            family = AF_INET;
        }
        else if (p == ListenerProtocol::IPv6)
        {
            pname = "IPv6";
            family = AF_INET6;
        }
        else if (p == ListenerProtocol::Unix)
        {
            pname = "unix socket";
            family = AF_UNIX;
        }

        std::ostringstream logtext;

        try
        {
            if (family == AF_UNIX)
            {
                logtext << "Creating unix socket listener on " << listener->unixSocketPath;
            }
            else
            {
                logtext << "Creating " << pname << " " << listener->getProtocolName() << " ";

                if (listener->haproxy)
                    logtext << "haproxy ";

                logtext << "listener on [" << listener->getBindAddress(p) << "]:" << listener->port;
            }

            logger->log(LOG_NOTICE) << logtext.str();

            BindAddr bindAddr(family, listener->getBindAddress(p), listener->port, listener->unixSocketUser, listener->unixSocketGroup, listener->unixSocketMode);

            ScopedSocket uniqueListenFd(check<std::runtime_error>(socket(family, SOCK_STREAM, 0)), listener->unixSocketPath, listener);

            if (p == ListenerProtocol::Unix)
            {
                unlink_if_sock(listener->unixSocketPath);
            }
            else
            {
                // Not needed for now. Maybe I will make multiple accept threads later, with SO_REUSEPORT.
                int optval = 1;
                check<std::runtime_error>(setsockopt(uniqueListenFd.get(), SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval)));

                if (listener->isTcpNoDelay())
                {
                    int tcp_nodelay_optval = 1;
                    check<std::runtime_error>(setsockopt(uniqueListenFd.get(), IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay_optval, sizeof(tcp_nodelay_optval)));
                }
            }

            int flags = fcntl(uniqueListenFd.get(), F_GETFL);
            check<std::runtime_error>(fcntl(uniqueListenFd.get(), F_SETFL, flags | O_NONBLOCK ));

            bindAddr.bind_socket(uniqueListenFd.get());
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
            logger->log(LOG_ERR) << logtext.str() << " failed: " << ex.what();
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
    for (ThreadDataOwner &thread : threads)
    {
        thread->queueDoKeepAliveCheck();
    }
}

void MainApp::queuePasswordFileReloadAllThreads()
{
    for (ThreadDataOwner &thread : threads)
    {
        thread->queuePasswdFileReload();
    }
}

void MainApp::queuepluginPeriodicEventAllThreads()
{
    for (ThreadDataOwner &thread : threads)
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
    if (!threads.empty())
    {
        std::vector<std::shared_ptr<ThreadData>> thread_datas;

        for(ThreadDataOwner &t : threads)
        {
            thread_datas.push_back(t.getThreadData());
        }

        threads.at(0)->queuePublishStatsOnDollarTopic(thread_datas);
    }
}

/**
 * @brief MainApp::saveStateInThread starts a thread for disk IO, because file IO is not async.
 */
void MainApp::saveStateInThread()
{
    std::list<BridgeInfoForSerializing> bridgeInfos = BridgeInfoForSerializing::getBridgeInfosForSerializing(this->bridgeConfigs);

    auto f = std::bind(&MainApp::saveState, this->settings, bridgeInfos, true);
    this->bgWorker.addTask(f, true);
}

void MainApp::queueSendQueuedWills()
{
    if (!threads.empty())
    {
        int threadnr = rand() % threads.size();
        std::shared_ptr<ThreadData> t = threads[threadnr].getThreadData();
        t->queueSendingQueuedWills();
    }
}

void MainApp::waitForWillsQueued()
{
    int i = 0;

    while(std::any_of(threads.begin(), threads.end(), [](const ThreadDataOwner &t){ return !t->allWillsQueued && t->running; }) && i++ < 5000)
    {
        usleep(1000);
    }
}

void MainApp::queueRetainedMessageExpiration()
{
    if (!threads.empty())
    {
        int threadnr = rand() % threads.size();
        std::shared_ptr<ThreadData> t = threads[threadnr].getThreadData();
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

        BridgeConfig &bridge = cur->second;

        std::shared_ptr<ThreadData> owner = bridge.owner.lock();

        if (!owner)
        {
            owner = threads.at(i++ % threads.size()).getThreadData();
            bridge.owner = owner;
        }

        if (bridge.queueForDelete)
        {
            owner->removeBridgeQueued(bridge, "Bridge disappeared from config");
            this->bridgeConfigs.erase(cur);
        }
        else
        {
            std::shared_ptr<BridgeState> bridgeState = std::make_shared<BridgeState>(bridge);
            bridgeState->threadData = owner;
            owner->giveBridge(bridgeState);
        }
    }
}

void MainApp::queueBridgeReconnectAllThreads(bool alsoQueueNexts)
{
    try
    {
        for (ThreadDataOwner &thread : threads)
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
        timed_tasks.addTask(fReconnectBridges, 5000, true);
    }
}

void MainApp::queueInternalHeartbeat()
{
    if (threads.empty())
        return;

    auto queue_time = std::chrono::steady_clock::now();

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

    std::transform(threads.begin(), threads.end(), drifts.begin(), [] (const ThreadDataOwner &t) {
        return t->driftCounter.getDrift();
    });

    const size_t n = drifts.size() / 2;
    std::nth_element(drifts.begin(), drifts.begin() + n, drifts.end());
    this->medianThreadDrift = drifts.at(n);

    for (ThreadDataOwner &thread : threads)
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
void MainApp::saveState(const Settings &settings, const std::list<BridgeInfoForSerializing> &bridgeInfos, bool in_background)
{
    Logger *logger = Logger::getInstance();

    try
    {
        if (!settings.storageDir.empty())
        {
            std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;

            const std::string retainedDBPath = settings.getRetainedMessagesDBFile();
            if (settings.retainedMessagesMode == RetainedMessagesMode::Enabled)
                subscriptionStore->saveRetainedMessages(retainedDBPath, in_background);
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

std::list<BridgeConfig> MainApp::loadBridgeInfo(Settings &settings)
{
    Logger *logger = Logger::getInstance();
    std::list<BridgeConfig> bridges = settings.stealBridges();

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
            for(BridgeConfig &bridgeConfig : bridges)
            {
                if (!bridgeConfig.useSavedClientId)
                    continue;

                if (bridgeConfig.clientidPrefix == info.prefix)
                {
                    logger->log(LOG_INFO) << "Assigning stored bridge clientid '" << info.clientId << "' to bridge '" << info.prefix << "'.";
                    bridgeConfig.setClientId(info.prefix, info.clientId);
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

    if (optind < argc)
    {
        throw std::runtime_error("Error: positional arguments given. Did you mean --config-file <path>?");
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
        ConnectionProtocol connectionProtocol = strContains(fuzzFilePathLower, "web") ? ConnectionProtocol::WebsocketMqtt : ConnectionProtocol::Mqtt;

        try
        {
            const std::string empty;

            std::vector<MqttPacket> packetQueueIn;
            std::vector<std::string> subtopics;

            std::shared_ptr<PluginLoader> pluginLoader = std::make_shared<PluginLoader>();

            std::shared_ptr<ThreadData> threaddata = std::make_shared<ThreadData>(0, settings, pluginLoader);
            ThreadGlobals::assignThreadData(threaddata);

            std::shared_ptr<Client> client = std::make_shared<Client>(ClientType::Normal, fd, threaddata, FmqSsl(), connectionProtocol, false, nullptr, settings, true);
            std::shared_ptr<Client> subscriber = std::make_shared<Client>(ClientType::Normal, fdnull, threaddata, FmqSsl(), connectionProtocol, false, nullptr, settings, true);
            subscriber->setClientProperties(ProtocolVersion::Mqtt311, "subscriber", {}, "subuser", true, 60);
            subscriber->setAuthenticated(true);

            std::shared_ptr<Client> websocketsubscriber = std::make_shared<Client>(ClientType::Normal, fdnull2, threaddata, FmqSsl(), ConnectionProtocol::WebsocketMqtt, false, nullptr, settings, true);
            websocketsubscriber->setClientProperties(ProtocolVersion::Mqtt311, "websocketsubscriber", {}, "websocksubuser", true, 60);
            websocketsubscriber->setAuthenticated(true);
            websocketsubscriber->setFakeUpgraded();
            subscriptionStore->registerClientAndKickExistingOne(websocketsubscriber);
            subtopics = splitTopic("#");
            subscriptionStore->addSubscription(websocketsubscriber->getSession(), subtopics, 0, false, false, empty, 0);

            subscriptionStore->registerClientAndKickExistingOne(subscriber);
            subscriptionStore->addSubscription(subscriber->getSession(), subtopics, 0, false, false, empty, 0);

            if (connectionProtocol == ConnectionProtocol::WebsocketMqtt && strContains(fuzzFilePathLower, "upgrade"))
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
                    packet.handle(client);
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

    std::shared_ptr<PluginLoader> pluginLoader = std::make_shared<PluginLoader>();
    pluginLoader->loadPlugin(settings.pluginPath);

    std::unordered_map<std::string, std::string> &authOpts = settings.getFlashmqpluginOpts();
    pluginLoader->mainInit(authOpts);

    for (int i = 0; i < num_threads; i++)
    {
        threads.emplace_back(i, settings, pluginLoader);
        threads.back().start();
    }

    // Populate the $SYS topics, otherwise you have to wait until the timer expires.
    if (!threads.empty())
    {
        std::vector<std::shared_ptr<ThreadData>> thread_datas;

        for(ThreadDataOwner &t : threads)
        {
            thread_datas.push_back(t.getThreadData());
        }

        threads.front()->queuePublishStatsOnDollarTopic(thread_datas);
    }

    sendBridgesToThreads();
    queueBridgeReconnectAllThreads(true);

    std::minstd_rand randomish;
    randomish.seed(get_random_int<unsigned long>());

    this->bgWorker.start();

    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    started = true;
    while (running)
    {
        const uint32_t next_task_delay = timed_tasks.getTimeTillNext();
        const uint32_t epoll_wait_time = std::min<uint32_t>(next_task_delay, 100);

        int num_fds = epoll_wait(this->epollFdAccept, events, MAX_EVENTS, epoll_wait_time);

        if (epoll_wait_time == 0)
            timed_tasks.performAll();

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

                    std::shared_ptr<ThreadData> thread_data = threads[listener->next_thread_index++ % num_threads].getThreadData();

                    logger->logf(LOG_DEBUG, "Accepting connection on thread %d on %s", thread_data->threadnr, listener->getProtocolName().c_str());

                    struct sockaddr_storage addr_mem;
                    std::memset(&addr_mem, 0, sizeof(addr_mem));
                    struct sockaddr *addr = reinterpret_cast<sockaddr*>(&addr_mem);
                    socklen_t len = sizeof(addr_mem);
                    int fd = check<std::runtime_error>(accept(cur_fd, addr, &len));

                    if (!listener->isAllowed(addr))
                    {
                        std::ostringstream oss;
                        oss << "Connection from " << sockaddrToString(addr) << " not allowed on " << listener->getProtocolName();

                        if (timed_tasks.getTaskCount() < 1000)
                        {
                            const uint32_t delay = (randomish() & 0x0FFF) + 1000;
                            oss << ". Closing after " << delay << " ms.";

                            auto close_f = [fd]()
                            {
                                close(fd);
                            };

                            timed_tasks.addTask(close_f, delay);
                        }
                        else
                        {
                            oss << ". Closing now.";
                            close(fd);
                        }

                        logger->log(LOG_NOTICE) << oss.str();

                        continue;
                    }

                    /*
                     * I decided to not use a delayed close mechanism. It has been observed that under overload and clients in a reconnect loop,
                     * you can collect open files up to (a) million(s). By accepting and closing, the hope is we can keep clients at bay from
                     * the thread loops well enough.
                     */
                    if (this->medianThreadDrift > settings.maxEventLoopDrift || this->drift.getDrift() > settings.maxEventLoopDrift)
                    {
                        const std::string addr_s = sockaddrToString(addr);
                        bool do_close = false;
                        const OverloadMode overload_mode = listener->overloadMode.value_or(settings.overloadMode);

                        if (overload_mode == OverloadMode::CloseNewClients)
                        {
                            if (overloadLogCounter <= OVERLOAD_LOGS_MUTE_AFTER_LINES)
                            {
                                overloadLogCounter++;
                                logger->log(LOG_ERROR) << "[OVERLOAD] FlashMQ seems to be overloaded while accepting new connection(s) from '"
                                                       << addr_s << ". Closing socket. See 'overload_mode' and 'max_event_loop_drift'.";
                            }
                            do_close = true;
                        }
                        else if (overload_mode == OverloadMode::Log)
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

                    FmqSsl clientSSL;
                    if (listener->isSsl())
                    {
                        if (!listener->sslctx)
                        {
                            logger->log(LOG_ERR) << "Listener is SSL but SSL context is null. Application bug.";
                            close(fd);
                            continue;
                        }

                        clientSSL = FmqSsl(*listener->sslctx);

                        if (!clientSSL)
                        {
                            logger->logf(LOG_ERR, "Problem creating SSL object. Closing client.");
                            close(fd);
                            continue;
                        }

                        clientSSL.set_fd(fd);
                    }

                    // Don't use std::make_shared to avoid the weak pointers keeping the control block in memory.
                    std::shared_ptr<Client> client = std::shared_ptr<Client>(new Client(
                        ClientType::Normal, fd, thread_data, std::move(clientSSL), listener->connectionProtocol, listener->isHaProxy(), addr, settings));

                    if (listener->getX509ClientVerficationMode() != X509ClientVerification::None)
                    {
                        client->setSslVerify(listener->getX509ClientVerficationMode());
                    }

                    client->setAllowAnonymousOverride(listener->allowAnonymous);
                    client->setAcmeRedirect(listener->acmeRedirectURL);

                    if (listener->maxBufferSize)
                        client->setMaxBufSizeOverride(listener->maxBufferSize.value());

                    thread_data->giveClient(std::move(client));

                    globals->stats.socketConnects.inc();
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
                }
            }
            catch (std::exception &ex)
            {
                logger->logf(LOG_ERR, "Problem in main thread: %s", ex.what());
            }

        }
    }

    activeListenSockets.clear();

    this->bgWorker.stop();

    if (settings.willsEnabled)
    {
        logger->logf(LOG_DEBUG, "Having all client in all threads send or queue their will.");
        for(ThreadDataOwner &thread : threads)
        {
            thread->queueSendWills();
        }
        waitForWillsQueued();
    }

    logger->logf(LOG_DEBUG, "Having all client in all threads send a disconnect packet and initiate quit.");
    for(ThreadDataOwner &thread : threads)
    {
        thread->queueSendDisconnects();
    }

    oneInstanceLock.unlock();

    {
        logger->logf(LOG_DEBUG, "Waiting for our own quit event to have been queued.");
        int count = 0;
        while(std::any_of(threads.begin(), threads.end(), [](const ThreadDataOwner &t){ return t->running; }))
        {
            if (count++ >= 30000)
                break;
            usleep(1000);
        }
    }

    logger->logf(LOG_DEBUG, "Waiting for threads clean-up functions to finish.");
    int count = 0;
    bool waitTimeExpired = false;
    while(std::any_of(threads.begin(), threads.end(), [](ThreadDataOwner &t){ return !t->finished; }))
    {
        if (count++ >= 30000)
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
        for(ThreadDataOwner &thread : threads)
        {
            logger->logf(LOG_DEBUG, "Waiting for thread %d to join.", thread->threadnr);
            thread.waitForQuit();
        }
    }

    pluginLoader->mainDeinit(settings.getFlashmqpluginOpts());

    globals->quitting = true;
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
    const Settings oldSettings = settings;
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
            pair.second.queueForDelete = true;
        }

        std::list<BridgeConfig> bridges = loadBridgeInfo(this->settings);

        for (BridgeConfig &bridge : bridges)
        {
            auto pos = this->bridgeConfigs.find(bridge.clientidPrefix);
            if (pos != this->bridgeConfigs.end())
            {
                logger->log(LOG_NOTICE) << "Assing new config to bridge '" << bridge.clientidPrefix << "' and reconnect if needed.";

                BridgeConfig &cur = pos->second;

                std::shared_ptr<ThreadData> owner = cur.owner.lock();
                std::string clientid = cur.getClientid();
                cur = bridge;
                cur.owner = owner;
                cur.setClientId(cur.clientidPrefix, clientid);
            }
            else
            {
                logger->log(LOG_NOTICE) << "Adding bridge '" << bridge.clientidPrefix << "'.";
                this->bridgeConfigs[bridge.clientidPrefix] = bridge;
            }
        }

        for (auto &pair : bridgeConfigs)
        {
            if (pair.second.queueForDelete)
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

    for (ThreadDataOwner &thread : threads)
    {
        thread->queueReload(settings);
    }

    reloadTimers(reload, oldSettings);
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

void MainApp::reloadTimers(bool reload, const Settings &old_settings)
{
    /*
     * Avoid postponing timed events when people continously send sighup signals.
     *
     * TODO: better method, that is not susceptible to forgetting adding settings here when more timers can change.
     */
    if (reload && settings.pluginTimerPeriod == old_settings.pluginTimerPeriod && settings.saveStateInterval == old_settings.saveStateInterval)
    {
        logger->log(LOG_NOTICE) << "Timer config not changed. Not re-adding timers.";
        return;
    }

    if (reload)
        logger->log(LOG_NOTICE) << "Settings impacting timed events changed. Re-adding timers.";
    else
        logger->log(LOG_NOTICE) << "Adding timers";

    timed_tasks.clear();

    if (settings.pluginTimerPeriod > 0)
    {
        auto fpluginPeriodicEvent = std::bind(&MainApp::queuepluginPeriodicEventAllThreads, this);
        timed_tasks.addTask(fpluginPeriodicEvent, settings.pluginTimerPeriod * 1000, true);
    }

    {
        auto fSaveState = std::bind(&MainApp::saveStateInThread, this);
        timed_tasks.addTask(fSaveState, settings.saveStateInterval.count() * 1000, true);
    }

    {
        auto f = std::bind(&MainApp::queueCleanup, this);
        //const uint64_t derrivedSessionCheckInterval = std::max<uint64_t>((settings.expireSessionsAfterSeconds)*1000*2, 600000);
        //const uint64_t sessionCheckInterval = std::min<uint64_t>(derrivedSessionCheckInterval, 86400000);
        uint32_t interval = 10000;
#ifdef TESTING
        interval = 1000;
#endif
        timed_tasks.addTask(f, interval, true);
    }

    {
        uint32_t interval = 1846849; // prime
        auto f = std::bind(&MainApp::queuePurgeSubscriptionTree, this);
        timed_tasks.addTask(f, interval, true);
    }

    {
        uint32_t interval = 3949193; // prime
#ifdef TESTING
        interval = 500;
#endif
        auto f = std::bind(&MainApp::queueRetainedMessageExpiration, this);
        timed_tasks.addTask(f, interval, true);
    }

    {
        auto fKeepAlive = std::bind(&MainApp::queueKeepAliveCheckAtAllThreads, this);
        timed_tasks.addTask(fKeepAlive, 5000, true);
    }

    {
        auto fPasswordFileReload = std::bind(&MainApp::queuePasswordFileReloadAllThreads, this);
        timed_tasks.addTask(fPasswordFileReload, 2000, true);
    }

    {
        auto fPublishStats = std::bind(&MainApp::queuePublishStatsOnDollarTopic, this);
        timed_tasks.addTask(fPublishStats, 10000, true);
    }

    {
        auto fSendPendingWills = std::bind(&MainApp::queueSendQueuedWills, this);
        timed_tasks.addTask(fSendPendingWills, 2000, true);
    }

    {
        auto fInternalHeartbeat = std::bind(&MainApp::queueInternalHeartbeat, this);
        timed_tasks.addTask(fInternalHeartbeat, HEARTBEAT_INTERVAL, true);
    }
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
        std::shared_ptr<ThreadData> t = threads[threadnr].getThreadData();
        t->queueRemoveExpiredSessions();
    }
}

void MainApp::queuePurgeSubscriptionTree()
{
    if (!threads.empty())
    {
        int threadnr = rand() % threads.size();
        std::shared_ptr<ThreadData> t = threads[threadnr].getThreadData();
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


