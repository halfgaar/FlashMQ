#include "mainapp.h"
#include "cassert"
#include "exceptions.h"
#include "getopt.h"
#include <unistd.h>
#include <stdio.h>

#define MAX_EVENTS 1024
#define NR_OF_THREADS 4

#define VERSION "0.1"

MainApp *MainApp::instance = nullptr;

void do_thread_work(ThreadData *threadData)
{
    int epoll_fd = threadData->epollfd;

    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    std::vector<MqttPacket> packetQueueIn;
    time_t lastKeepAliveCheck = time(NULL);

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

                Client_p client = threadData->getClient(fd);

                if (client)
                {
                    try
                    {
                        if (cur_ev.events & EPOLLIN)
                        {
                            bool readSuccess = client->readFdIntoBuffer();
                            client->bufferToMqttPackets(packetQueueIn, client);

                            if (!readSuccess)
                            {
                                threadData->removeClient(client);
                                continue;
                            }
                        }
                        if (cur_ev.events & EPOLLOUT)
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
                        if (cur_ev.events & (EPOLLERR | EPOLLHUP))
                        {
                            threadData->removeClient(client);
                        }
                    }
                    catch(std::exception &ex)
                    {
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
                logger->logf(LOG_ERR, "MqttPacket handling error: %s. Removing client.", ex.what());
                threadData->removeClient(packet.getSender());
            }
        }
        packetQueueIn.clear();

        try
        {
            if (lastKeepAliveCheck + 30 < time(NULL))
            {
                if (threadData->doKeepAliveCheck())
                    lastKeepAliveCheck = time(NULL);
            }
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Error handling keep-alives: %s.", ex.what());
        }
    }
}



MainApp::MainApp(const std::string &configFilePath) :
    subscriptionStore(new SubscriptionStore())
{
    taskEventFd = eventfd(0, EFD_NONBLOCK);

    confFileParser.reset(new ConfigFileParser(configFilePath));
    loadConfig();
}

void MainApp::doHelp(const char *arg)
{
    puts("FlashMQ - the scalable light-weight MQTT broker");
    puts("");
    printf("Usage: %s [options]\n", arg);
    puts("");
    puts(" -h, --help                           Print help");
    puts(" -c, --config-file <flashmq.conf>     Configuration file.");
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

void MainApp::initMainApp(int argc, char *argv[])
{
    if (instance != nullptr)
        throw std::runtime_error("App was already initialized.");

    static struct option long_options[] =
    {
        {"help", no_argument, nullptr, 'h'},
        {"config-file", required_argument, nullptr, 'c'},
        {"version", no_argument, nullptr, 'V'},
        {"license", no_argument, nullptr, 'l'},
        {nullptr, 0, nullptr, 0}
    };

    std::string configFile;

    int option_index = 0;
    int opt;
    while((opt = getopt_long(argc, argv, "hc:Vl", long_options, &option_index)) != -1)
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
        case '?':
            MainApp::doHelp(argv[0]);
            exit(16);
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
    Logger *logger = Logger::getInstance();

    int listen_fd = check<std::runtime_error>(socket(AF_INET, SOCK_STREAM, 0));

    // Not needed for now. Maybe I will make multiple accept threads later, with SO_REUSEPORT.
    int optval = 1;
    check<std::runtime_error>(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval)));

    int flags = fcntl(listen_fd, F_GETFL);
    check<std::runtime_error>(fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK ));

    struct sockaddr_in in_addr;
    in_addr.sin_family = AF_INET;
    in_addr.sin_addr.s_addr = INADDR_ANY;
    in_addr.sin_port = htons(1883);

    check<std::runtime_error>(bind(listen_fd, (struct sockaddr *)(&in_addr), sizeof(struct sockaddr_in)));
    check<std::runtime_error>(listen(listen_fd, 1024));

    int epoll_fd_accept = check<std::runtime_error>(epoll_create(999));

    struct epoll_event events[MAX_EVENTS];
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    ev.data.fd = listen_fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epoll_fd_accept, EPOLL_CTL_ADD, listen_fd, &ev));

    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = taskEventFd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epoll_fd_accept, EPOLL_CTL_ADD, taskEventFd, &ev));

    for (int i = 0; i < NR_OF_THREADS; i++)
    {
        std::shared_ptr<ThreadData> t(new ThreadData(i, subscriptionStore, *confFileParser.get()));
        t->start(&do_thread_work);
        threads.push_back(t);
    }

    logger->logf(LOG_NOTICE, "Listening on port 1883");

    uint next_thread_index = 0;

    started = true;
    while (running)
    {
        int num_fds = epoll_wait(epoll_fd_accept, events, MAX_EVENTS, 100);

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
                if (cur_fd == listen_fd)
                {
                    std::shared_ptr<ThreadData> thread_data = threads[next_thread_index++ % NR_OF_THREADS];

                    logger->logf(LOG_INFO, "Accepting connection on thread %d", thread_data->threadnr);

                    struct sockaddr addr;
                    memset(&addr, 0, sizeof(struct sockaddr));
                    socklen_t len = sizeof(struct sockaddr);
                    int fd = check<std::runtime_error>(accept(cur_fd, &addr, &len));

                    Client_p client(new Client(fd, thread_data));
                    thread_data->giveClient(client);
                }
                else if (cur_fd == taskEventFd)
                {
                    uint64_t eventfd_value = 0;
                    check<std::runtime_error>(read(cur_fd, &eventfd_value, sizeof(uint64_t)));

                    for(auto &f : taskQueue)
                    {
                        f();
                    }
                    taskQueue.clear();
                }
                else
                {
                    throw std::runtime_error("Bug: the main thread had activity on an fd it's not supposed to monitor.");
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
        thread->quit();
    }

    close(listen_fd);
}

void MainApp::quit()
{
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_NOTICE, "Quitting FlashMQ");
    running = false;
}

void MainApp::loadConfig()
{
    Logger *logger = Logger::getInstance();
    confFileParser->loadFile();
    logger->setLogPath(confFileParser->logPath);
    logger->reOpen();
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

    uint64_t one = 1;
    write(taskEventFd, &one, sizeof(uint64_t));
}
