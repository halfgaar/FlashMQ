#include "mainapp.h"
#include "cassert"
#include "exceptions.h"
#include "getopt.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/sysinfo.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "logger.h"

#define MAX_EVENTS 1024

#define VERSION "0.1"

MainApp *MainApp::instance = nullptr;

void do_thread_work(ThreadData *threadData)
{
    int epoll_fd = threadData->epollfd;

    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    std::vector<MqttPacket> packetQueueIn;

    Logger *logger = Logger::getInstance();

    try
    {
        logger->logf(LOG_NOTICE, "Thread %d doing auth init.", threadData->threadnr);
        threadData->initAuthPlugin();
    }
    catch(std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error initializing auth back-end: %s", ex.what());
        threadData->running = false;
        MainApp *instance = MainApp::getMainApp();
        instance->quit();
    }

    while (threadData->running)
    {
        int fdcount = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);

        if (fdcount < 0)
        {
            if (errno == EINTR)
                continue;
            logger->logf(LOG_ERR, "Problem waiting for fd: %s", strerror(errno));
        }
        else if (fdcount > 0)
        {
            for (int i = 0; i < fdcount; i++)
            {
                struct epoll_event cur_ev = events[i];
                int fd = cur_ev.data.fd;

                if (fd == threadData->taskEventFd)
                {
                    uint64_t eventfd_value = 0;
                    check<std::runtime_error>(read(fd, &eventfd_value, sizeof(uint64_t)));

                    std::lock_guard<std::mutex> locker(threadData->taskQueueMutex);
                    for(auto &f : threadData->taskQueue)
                    {
                        f();
                    }
                    threadData->taskQueue.clear();

                    continue;
                }

                Client_p client = threadData->getClient(fd);

                if (client)
                {
                    try
                    {
                        if (cur_ev.events & (EPOLLERR | EPOLLHUP))
                        {
                            client->setDisconnectReason("epoll says socket is in ERR or HUP state.");
                            threadData->removeClient(client);
                            continue;
                        }
                        if (client->isSsl() && !client->isSslAccepted())
                        {
                            client->startOrContinueSslAccept();
                            continue;
                        }
                        if ((cur_ev.events & EPOLLIN) || ((cur_ev.events & EPOLLOUT) && client->getSslReadWantsWrite()))
                        {
                            bool readSuccess = client->readFdIntoBuffer();
                            client->bufferToMqttPackets(packetQueueIn, client);

                            if (!readSuccess)
                            {
                                client->setDisconnectReason("socket disconnect detected");
                                threadData->removeClient(client);
                                continue;
                            }
                        }
                        if ((cur_ev.events & EPOLLOUT) || ((cur_ev.events & EPOLLIN) && client->getSslWriteWantsRead()))
                        {
                            if (!client->writeBufIntoFd())
                            {
                                threadData->removeClient(client);
                                continue;
                            }

                            if (client->readyForDisconnecting())
                            {
                                threadData->removeClient(client);
                                continue;
                            }
                        }
                    }
                    catch(std::exception &ex)
                    {
                        client->setDisconnectReason(ex.what());
                        logger->logf(LOG_ERR, "Packet read/write error: %s. Removing client.", ex.what());
                        threadData->removeClient(client);
                    }
                }
            }
        }

        for (MqttPacket &packet : packetQueueIn)
        {
            try
            {
                packet.handle();
            }
            catch (std::exception &ex)
            {
                packet.getSender()->setDisconnectReason(ex.what());
                logger->logf(LOG_ERR, "MqttPacket handling error: %s. Removing client.", ex.what());
                threadData->removeClient(packet.getSender());
            }
        }
        packetQueueIn.clear();
    }
}



MainApp::MainApp(const std::string &configFilePath) :
    subscriptionStore(new SubscriptionStore())
{
    this->num_threads = get_nprocs();

    if (num_threads <= 0)
        throw std::runtime_error("Invalid number of CPUs: " + std::to_string(num_threads));

    epollFdAccept = check<std::runtime_error>(epoll_create(999));
    taskEventFd = eventfd(0, EFD_NONBLOCK);

    confFileParser.reset(new ConfigFileParser(configFilePath));
    loadConfig();

    // TODO: override in conf possibility.
    logger->logf(LOG_NOTICE, "%d CPUs are detected, making as many threads.", num_threads);

    auto f = std::bind(&MainApp::queueCleanup, this);
    timer.addCallback(f, 86400000, "session expiration");

    auto fKeepAlive = std::bind(&MainApp::queueKeepAliveCheckAtAllThreads, this);
    timer.addCallback(fKeepAlive, 30000, "keep-alive check");
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
    puts(" -c, --config-file <flashmq.conf>     Configuration file.");
    puts(" -t, --test-config                    Test configuration file.");
    puts(" -V, --version                        Show version");
    puts(" -l, --license                        Show license");
}

void MainApp::showLicense()
{
    printf("FlashMQ Version %s\n", VERSION);
    puts("Copyright (C) 2021 Wiebe Cazemier.");
    puts("");
    puts("License to be decided");
    puts("");
    puts("Author: Wiebe Cazemier <wiebe@halfgaar.net>");
}

int MainApp::createListenSocket(const std::shared_ptr<Listener> &listener)
{
    if (listener->port <= 0)
        return -2;

    logger->logf(LOG_NOTICE, "Creating %s listener on port %d", listener->getProtocolName().c_str(), listener->port);

    try
    {
        int listen_fd = check<std::runtime_error>(socket(AF_INET, SOCK_STREAM, 0));

        // Not needed for now. Maybe I will make multiple accept threads later, with SO_REUSEPORT.
        int optval = 1;
        check<std::runtime_error>(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval)));

        int flags = fcntl(listen_fd, F_GETFL);
        check<std::runtime_error>(fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK ));

        struct sockaddr_in in_addr_plain;
        in_addr_plain.sin_family = AF_INET;
        in_addr_plain.sin_addr.s_addr = INADDR_ANY;
        in_addr_plain.sin_port = htons(listener->port);

        check<std::runtime_error>(bind(listen_fd, (struct sockaddr *)(&in_addr_plain), sizeof(struct sockaddr_in)));
        check<std::runtime_error>(listen(listen_fd, 1024));

        struct epoll_event ev;
        memset(&ev, 0, sizeof (struct epoll_event));

        ev.data.fd = listen_fd;
        ev.events = EPOLLIN;
        check<std::runtime_error>(epoll_ctl(this->epollFdAccept, EPOLL_CTL_ADD, listen_fd, &ev));

        return listen_fd;
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_NOTICE, "Creating %s listener on port %d failed: %s", listener->getProtocolName().c_str(), listener->port, ex.what());
        return -1;
    }
    return -1;
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

void MainApp::initMainApp(int argc, char *argv[])
{
    if (instance != nullptr)
        throw std::runtime_error("App was already initialized.");

    static struct option long_options[] =
    {
        {"help", no_argument, nullptr, 'h'},
        {"config-file", required_argument, nullptr, 'c'},
        {"test-config", no_argument, nullptr, 't'},
        {"version", no_argument, nullptr, 'V'},
        {"license", no_argument, nullptr, 'l'},
        {nullptr, 0, nullptr, 0}
    };

    std::string configFile;

    int option_index = 0;
    int opt;
    bool testConfig = false;
    while((opt = getopt_long(argc, argv, "hc:Vlt", long_options, &option_index)) != -1)
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
                std::cerr << "No config specified." << std::endl;
                MainApp::doHelp(argv[0]);
                exit(1);
            }

            ConfigFileParser c(configFile);
            c.loadFile(true);
            puts("Config OK");
            exit(0);
        }
        catch (ConfigFileException &ex)
        {
            std::cerr << ex.what() << std::endl;
            exit(1);
        }
    }

    instance = new MainApp(configFile);
}


MainApp *MainApp::getMainApp()
{
    if (!instance)
        throw std::runtime_error("You haven't initialized the app yet.");
    return instance;
}

void MainApp::start()
{
    timer.start();

    std::map<int, std::shared_ptr<Listener>> listenerMap;

    for(std::shared_ptr<Listener> &listener : this->listeners)
    {
        int fd = createListenSocket(listener);
        if (fd > 0)
            listenerMap[fd] = listener;
    }

#ifdef NDEBUG
    logger->noLongerLogToStd();
#endif

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = taskEventFd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(this->epollFdAccept, EPOLL_CTL_ADD, taskEventFd, &ev));

    for (int i = 0; i < num_threads; i++)
    {
        std::shared_ptr<ThreadData> t(new ThreadData(i, subscriptionStore, settings));
        t->start(&do_thread_work);
        threads.push_back(t);
    }

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

                    struct sockaddr addr;
                    memset(&addr, 0, sizeof(struct sockaddr));
                    socklen_t len = sizeof(struct sockaddr);
                    int fd = check<std::runtime_error>(accept(cur_fd, &addr, &len));

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

                    Client_p client(new Client(fd, thread_data, clientSSL, listener->websocket, settings));
                    thread_data->giveClient(client);
                }
                else
                {
                    uint64_t eventfd_value = 0;
                    check<std::runtime_error>(read(cur_fd, &eventfd_value, sizeof(uint64_t)));

                    std::lock_guard<std::mutex> locker(eventMutex);
                    for(auto &f : taskQueue)
                    {
                        f();
                    }
                    taskQueue.clear();
                }
            }
            catch (std::exception &ex)
            {
                logger->logf(LOG_ERR, "Problem in main thread: %s", ex.what());
            }

        }
    }

    for(std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->queueQuit();
    }

    for(std::shared_ptr<ThreadData> &thread : threads)
    {
        thread->waitForQuit();
    }

    for(auto pair : listenerMap)
    {
        close(pair.first);
    }
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

// Loaded on app start where you want it to crash, loaded from within try/catch on reload, to allow the program to continue.
void MainApp::loadConfig()
{
    Logger *logger = Logger::getInstance();

    // Atomic loading, first test.
    confFileParser->loadFile(true);
    confFileParser->loadFile(false);
    settings = std::move(confFileParser->settings);

    if (settings->listeners.empty())
    {
        std::shared_ptr<Listener> defaultListener(new Listener());
        defaultListener->isValid();
        settings->listeners.push_back(defaultListener);
    }

    // For now, it's too much work to be able to reload new listeners, with all the shared resource stuff going on. So, I'm
    // loading them to a local var which is never updated.
    if (listeners.empty())
        listeners = settings->listeners;

    logger->setLogPath(settings->logPath);
    logger->reOpen();

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

    auto f = std::bind(&SubscriptionStore::removeExpiredSessionsClients, subscriptionStore.get());
    taskQueue.push_front(f);

    wakeUpThread();
}
