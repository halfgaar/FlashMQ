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

#include "mainapp.h"
#include "cassert"
#include "exceptions.h"
#include "getopt.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <arpa/inet.h>
#include <memory>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "logger.h"
#include "threadglobals.h"
#include "threadloop.h"
#include "authplugin.h"
#include "threadglobals.h"

MainApp *MainApp::instance = nullptr;

MainApp::MainApp(const std::string &configFilePath) :
    subscriptionStore(std::make_shared<SubscriptionStore>())
{
    epollFdAccept = check<std::runtime_error>(epoll_create(999));
    taskEventFd = eventfd(0, EFD_NONBLOCK);

    confFileParser = std::make_unique<ConfigFileParser>(configFilePath);
    loadConfig();

    this->num_threads = get_nprocs();
    if (settings->threadCount > 0)
    {
        this->num_threads = settings->threadCount;
        logger->logf(LOG_NOTICE, "%d threads specified by 'thread_count'.", num_threads);
    }
    else
    {
        logger->logf(LOG_NOTICE, "%d CPUs are detected, making as many threads. Use 'thread_count' setting to override.", num_threads);
    }

    if (num_threads <= 0)
        throw std::runtime_error("Invalid number of CPUs: " + std::to_string(num_threads));

    if (settings->expireSessionsAfterSeconds > 0)
    {
        auto f = std::bind(&MainApp::queueCleanup, this);
        //const uint64_t derrivedSessionCheckInterval = std::max<uint64_t>((settings->expireSessionsAfterSeconds)*1000*2, 600000);
        //const uint64_t sessionCheckInterval = std::min<uint64_t>(derrivedSessionCheckInterval, 86400000);
        timer.addCallback(f, 10000, "session expiration");
    }

    auto fKeepAlive = std::bind(&MainApp::queueKeepAliveCheckAtAllThreads, this);
    timer.addCallback(fKeepAlive, 30000, "keep-alive check");

    auto fPasswordFileReload = std::bind(&MainApp::queuePasswordFileReloadAllThreads, this);
    timer.addCallback(fPasswordFileReload, 2000, "Password file reload.");

    auto fPublishStats = std::bind(&MainApp::queuePublishStatsOnDollarTopic, this);
    timer.addCallback(fPublishStats, 10000, "Publish stats on $SYS");

    if (settings->authPluginTimerPeriod > 0)
    {
        auto fAuthPluginPeriodicEvent = std::bind(&MainApp::queueAuthPluginPeriodicEventAllThreads, this);
        timer.addCallback(fAuthPluginPeriodicEvent, settings->authPluginTimerPeriod*1000, "Auth plugin periodic event.");
    }

    if (!settings->storageDir.empty())
    {
        subscriptionStore->loadRetainedMessages(settings->getRetainedMessagesDBFile());
        subscriptionStore->loadSessionsAndSubscriptions(settings->getSessionsDBFile());
    }

    auto fSaveState = std::bind(&MainApp::saveStateInThread, this);
    timer.addCallback(fSaveState, 900000, "Save state.");

    auto fSendPendingWills = std::bind(&MainApp::queueSendQueuedWills, this);
    timer.addCallback(fSendPendingWills, 2000, "Publish pending wills.");
}

MainApp::~MainApp()
{
    if (epollFdAccept > 0)
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
    printf("FlashMQ Version %s\n", FLASHMQ_VERSION);
    puts("Copyright (C) 2021 Wiebe Cazemier.");
    puts("License AGPLv3: GNU AGPL version 3. <https://www.gnu.org/licenses/agpl-3.0.html>.");
    puts("");
    puts("Author: Wiebe Cazemier <wiebe@halfgaar.net>");
}

std::list<ScopedSocket> MainApp::createListenSocket(const std::shared_ptr<Listener> &listener)
{
    std::list<ScopedSocket> result;

    if (listener->port <= 0)
        return result;

    for (ListenerProtocol p : std::list<ListenerProtocol>({ ListenerProtocol::IPv4, ListenerProtocol::IPv6}))
    {
        std::string pname = p == ListenerProtocol::IPv4 ? "IPv4" : "IPv6";
        int family = p == ListenerProtocol::IPv4 ? AF_INET : AF_INET6;

        if (!(listener->protocol == ListenerProtocol::IPv46 || listener->protocol == p))
            continue;

        try
        {
            logger->logf(LOG_NOTICE, "Creating %s %s listener on [%s]:%d", pname.c_str(), listener->getProtocolName().c_str(),
                         listener->getBindAddress(p).c_str(), listener->port);

            BindAddr bindAddr = getBindAddr(family, listener->getBindAddress(p), listener->port);

            int listen_fd = check<std::runtime_error>(socket(family, SOCK_STREAM, 0));

            // Not needed for now. Maybe I will make multiple accept threads later, with SO_REUSEPORT.
            int optval = 1;
            check<std::runtime_error>(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval)));

            int flags = fcntl(listen_fd, F_GETFL);
            check<std::runtime_error>(fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK ));

            check<std::runtime_error>(bind(listen_fd, bindAddr.p.get(), bindAddr.len));
            check<std::runtime_error>(listen(listen_fd, 32768));

            struct epoll_event ev;
            memset(&ev, 0, sizeof (struct epoll_event));

            ev.data.fd = listen_fd;
            ev.events = EPOLLIN;
            check<std::runtime_error>(epoll_ctl(this->epollFdAccept, EPOLL_CTL_ADD, listen_fd, &ev));

            result.push_back(ScopedSocket(listen_fd));

        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Creating %s %s listener on [%s]:%d failed: %s", pname.c_str(), listener->getProtocolName().c_str(),
                         listener->getBindAddress(p).c_str(), listener->port, ex.what());
            return std::list<ScopedSocket>();
        }
    }

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

void MainApp::queueAuthPluginPeriodicEventAllThreads()
{
    for (std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueAuthPluginPeriodicEvent();
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
        taskQueue.push_front(f);

        wakeUpThread();
    }
}

/**
 * @brief MainApp::saveStateInThread starts a thread for disk IO, because file IO is not async.
 */
void MainApp::saveStateInThread()
{
    // Prevent queueing it again when it's still running.
    std::unique_lock<std::mutex> locker(saveStateMutex, std::try_to_lock);
    if (!locker.owns_lock())
        return;

    // Join previous instances.
    if (saveStateThread.joinable())
        saveStateThread.join();

    auto f = std::bind(&MainApp::saveState, this);
    saveStateThread = std::thread(f);

    pthread_t native = saveStateThread.native_handle();
    pthread_setname_np(native, "SaveState");
}

void MainApp::queueSendQueuedWills()
{
    std::lock_guard<std::mutex> locker(eventMutex);

    if (!threads.empty())
    {
        std::shared_ptr<ThreadData> t = threads[nextThreadForTasks++ % threads.size()];
        auto f = std::bind(&ThreadData::queueSendingQueuedWills, t.get());
        taskQueue.push_front(f);

        wakeUpThread();
    }
}

void MainApp::queueRemoveExpiredSessions()
{
    std::lock_guard<std::mutex> locker(eventMutex);

    if (!threads.empty())
    {
        std::shared_ptr<ThreadData> t = threads[nextThreadForTasks++ % threads.size()];
        auto f = std::bind(&ThreadData::queueRemoveExpiredSessions, t.get());
        taskQueue.push_front(f);

        wakeUpThread();
    }
}

void MainApp::saveState()
{
    std::lock_guard<std::mutex> lg(saveStateMutex);

    try
    {
        if (!settings->storageDir.empty())
        {
            const std::string retainedDBPath = settings->getRetainedMessagesDBFile();
            subscriptionStore->saveRetainedMessages(retainedDBPath);

            const std::string sessionsDBPath = settings->getSessionsDBFile();
            subscriptionStore->saveSessionsAndSubscriptions(sessionsDBPath);

            logger->logf(LOG_INFO, "Saving states done");
        }
    }
    catch(std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error saving state: %s", ex.what());
    }
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
    if (fuzzFilePath.empty())
    {
        oneInstanceLock.lock();
    }
#endif

    timer.start();

    std::map<int, std::shared_ptr<Listener>> listenerMap; // For finding listeners by fd.
    std::list<ScopedSocket> activeListenSockets; // For RAII/ownership

    for(std::shared_ptr<Listener> &listener : this->listeners)
    {
        std::list<ScopedSocket> scopedSockets = createListenSocket(listener);

        for (ScopedSocket &scopedSocket : scopedSockets)
        {
            if (scopedSocket.socket > 0)
                listenerMap[scopedSocket.socket] = listener;
            activeListenSockets.push_back(std::move(scopedSocket));
        }
    }

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
    if (!fuzzFilePath.empty())
    {
        // No threads for execution stability/determinism.
        num_threads = 0;

        settings->allowAnonymous = true;

        int fd = open(fuzzFilePath.c_str(), O_RDONLY);
        assert(fd > 0);

        int fdnull = open("/dev/null", O_RDWR);
        assert(fdnull > 0);

        int fdnull2 = open("/dev/null", O_RDWR);
        assert(fdnull2 > 0);

        const std::string fuzzFilePathLower = str_tolower(fuzzFilePath);
        bool fuzzWebsockets = strContains(fuzzFilePathLower, "web");

        try
        {
            std::vector<MqttPacket> packetQueueIn;
            std::vector<std::string> subtopics;

            Authentication auth(settingsLocalCopy);
            ThreadGlobals::assign(&auth);

            std::shared_ptr<ThreadData> threaddata = std::make_shared<ThreadData>(0, subscriptionStore, settings);

            std::shared_ptr<Client> client = std::make_shared<Client>(fd, threaddata, nullptr, fuzzWebsockets, nullptr, settings, true);
            std::shared_ptr<Client> subscriber = std::make_shared<Client>(fdnull, threaddata, nullptr, fuzzWebsockets, nullptr, settings, true);
            subscriber->setClientProperties(ProtocolVersion::Mqtt311, "subscriber", "subuser", true, 60);
            subscriber->setAuthenticated(true);

            std::shared_ptr<Client> websocketsubscriber = std::make_shared<Client>(fdnull2, threaddata, nullptr, true, nullptr, settings, true);
            websocketsubscriber->setClientProperties(ProtocolVersion::Mqtt311, "websocketsubscriber", "websocksubuser", true, 60);
            websocketsubscriber->setAuthenticated(true);
            websocketsubscriber->setFakeUpgraded();
            subscriptionStore->registerClientAndKickExistingOne(websocketsubscriber);
            splitTopic("#", subtopics);
            subscriptionStore->addSubscription(websocketsubscriber, "#", subtopics, 0);

            subscriptionStore->registerClientAndKickExistingOne(subscriber);
            subscriptionStore->addSubscription(subscriber, "#", subtopics, 0);

            if (fuzzWebsockets && strContains(fuzzFilePathLower, "upgrade"))
            {
                client->setFakeUpgraded();
                subscriber->setFakeUpgraded();
            }

            client->readFdIntoBuffer();
            client->bufferToMqttPackets(packetQueueIn, client);

            for (MqttPacket &packet : packetQueueIn)
            {
                packet.handle();
            }

            subscriber->writeBufIntoFd();
            websocketsubscriber->writeBufIntoFd();
        }
        catch (ProtocolError &ex)
        {
            logger->logf(LOG_ERR, "Expected MqttPacket handling error: %s", ex.what());
        }

        running = false;
    }
#endif

    for (int i = 0; i < num_threads; i++)
    {
        std::shared_ptr<ThreadData> t = std::make_shared<ThreadData>(i, subscriptionStore, settings);
        t->start(&do_thread_work);
        threads.push_back(t);
    }

    // Populate the $SYS topics, otherwise you have to wait until the timer expires.
    if (!threads.empty())
        threads.front()->queuePublishStatsOnDollarTopic(threads);

    uint next_thread_index = 0;

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
                    std::shared_ptr<Listener> listener = listenerMap[cur_fd];
                    std::shared_ptr<ThreadData> thread_data = threads[next_thread_index++ % num_threads];

                    logger->logf(LOG_INFO, "Accepting connection on thread %d on %s", thread_data->threadnr, listener->getProtocolName().c_str());

                    struct sockaddr_in6 addrBiggest;
                    struct sockaddr *addr = reinterpret_cast<sockaddr*>(&addrBiggest);
                    socklen_t len = sizeof(struct sockaddr_in6);
                    memset(addr, 0, len);
                    int fd = check<std::runtime_error>(accept(cur_fd, addr, &len));

                    SSL *clientSSL = nullptr;
                    if (listener->isSsl())
                    {
                        clientSSL = SSL_new(listener->sslctx->get());

                        if (clientSSL == NULL)
                        {
                            logger->logf(LOG_ERR, "Problem creating SSL object. Closing client.");
                            close(fd);
                            continue;
                        }

                        SSL_set_fd(clientSSL, fd);
                    }

                    std::shared_ptr<Client> client = std::make_shared<Client>(fd, thread_data, clientSSL, listener->websocket, addr, settings);
                    thread_data->giveClient(client);
                }
                else
                {
                    uint64_t eventfd_value = 0;
                    check<std::runtime_error>(read(cur_fd, &eventfd_value, sizeof(uint64_t)));

                    std::forward_list<std::function<void()>> tasks;

                    {
                        std::lock_guard<std::mutex> locker(eventMutex);
                        for (auto &f : taskQueue)
                        {
                            tasks.push_front(f);
                        }
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
        if (count++ >= 5000)
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

    saveState();

    if (saveStateThread.joinable())
        saveStateThread.join();
}

void MainApp::quit()
{
    std::lock_guard<std::mutex> guard(quitMutex);
    if (!running)
        return;

    Logger *logger = Logger::getInstance();
    logger->logf(LOG_NOTICE, "Quitting FlashMQ");
    timer.stop();
    running = false;
}


void MainApp::setlimits()
{
    rlim_t nofile = settings->rlimitNoFile;
    logger->logf(LOG_INFO, "Setting rlimit nofile to %ld.", nofile);
    struct rlimit v = { nofile, nofile };
    if (setrlimit(RLIMIT_NOFILE, &v) < 0)
    {
        logger->logf(LOG_ERR, "Setting ulimit nofile failed: '%s'. This means the default is used.", strerror(errno));
    }
}

/**
 * @brief MainApp::loadConfig is loaded on app start where you want it to crash, loaded from within try/catch on reload, to allow the program to continue.
 */
void MainApp::loadConfig()
{
    Logger *logger = Logger::getInstance();

    // Atomic loading, first test.
    confFileParser->loadFile(true);
    confFileParser->loadFile(false);
    settings = confFileParser->moveSettings();
    settingsLocalCopy = *settings.get();
    ThreadGlobals::assignSettings(&settingsLocalCopy);

    if (settings->listeners.empty())
    {
        std::shared_ptr<Listener> defaultListener = std::make_shared<Listener>();
        defaultListener->isValid();
        settings->listeners.push_back(defaultListener);
    }

    // For now, it's too much work to be able to reload new listeners, with all the shared resource stuff going on. So, I'm
    // loading them to a local var which is never updated.
    if (listeners.empty())
        listeners = settings->listeners;

    logger->setLogPath(settings->logPath);
    logger->queueReOpen();
    logger->setFlags(settings->logDebug, settings->logSubscriptions, settings->quiet);

    setlimits();

    for (std::shared_ptr<Listener> &l : this->listeners)
    {
        l->loadCertAndKeyFromConfig();
    }

    for (std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueReload(settings);
    }
}

void MainApp::reloadConfig()
{
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_NOTICE, "Reloading config");

    try
    {
        loadConfig();
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error reloading config: %s", ex.what());
    }

}

void MainApp::queueConfigReload()
{
    std::lock_guard<std::mutex> locker(eventMutex);

    auto f = std::bind(&MainApp::reloadConfig, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void MainApp::queueCleanup()
{
    std::lock_guard<std::mutex> locker(eventMutex);

    auto f = std::bind(&MainApp::queueRemoveExpiredSessions, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

