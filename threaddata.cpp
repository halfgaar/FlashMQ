#include "threaddata.h"
#include <string>
#include <sstream>

ThreadData::ThreadData(int threadnr, std::shared_ptr<SubscriptionStore> &subscriptionStore, ConfigFileParser &confFileParser, const GlobalSettings &settings) :
    subscriptionStore(subscriptionStore),
    confFileParser(confFileParser),
    authPlugin(confFileParser),
    threadnr(threadnr),
    settingsLocalCopy(settings)
{
    logger = Logger::getInstance();

    epollfd = check<std::runtime_error>(epoll_create(999));

    taskEventFd = eventfd(0, EFD_NONBLOCK);
    if (taskEventFd < 0)
        throw std::runtime_error("Can't create eventfd.");

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = taskEventFd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(this->epollfd, EPOLL_CTL_ADD, taskEventFd, &ev));
}

void ThreadData::start(thread_f f)
{
    this->thread = std::thread(f, this);

    pthread_t native = this->thread.native_handle();
    std::ostringstream threadName;
    threadName << "FlashMQ T " << threadnr;
    threadName.flush();
    const char *c_str = threadName.str().c_str();
    pthread_setname_np(native, c_str);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(threadnr, &cpuset);
    check<std::runtime_error>(pthread_setaffinity_np(native, sizeof(cpuset), &cpuset));

    // It's not really necessary to get affinity again, but now I'm logging truth instead assumption.
    check<std::runtime_error>(pthread_getaffinity_np(native, sizeof(cpuset), &cpuset));
    int pinned_cpu = -1;
    for (int j = 0; j < CPU_SETSIZE; j++)
        if (CPU_ISSET(j, &cpuset))
            pinned_cpu = j;

    logger->logf(LOG_NOTICE, "Thread '%s' pinned to CPU %d", c_str, pinned_cpu);
}

void ThreadData::quit()
{
    running = false;
    thread.join();
}

void ThreadData::giveClient(Client_p client)
{
    clients_by_fd_mutex.lock();
    int fd = client->getFd();
    clients_by_fd[fd] = client;
    clients_by_fd_mutex.unlock();

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev));
}

Client_p ThreadData::getClient(int fd)
{
    std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
    return this->clients_by_fd[fd];
}

void ThreadData::removeClient(Client_p client)
{
    client->markAsDisconnecting();

    std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
    clients_by_fd.erase(client->getFd());
}

void ThreadData::removeClient(int fd)
{
    std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
    auto client_it = this->clients_by_fd.find(fd);
    if (client_it != this->clients_by_fd.end())
    {
        client_it->second->markAsDisconnecting();
        this->clients_by_fd.erase(fd);
    }
}

std::shared_ptr<SubscriptionStore> &ThreadData::getSubscriptionStore()
{
    return subscriptionStore;
}

void ThreadData::queueDoKeepAliveCheck()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::doKeepAliveCheck, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

// TODO: profile how fast hash iteration is. Perhaps having a second list/vector is beneficial?
void ThreadData::doKeepAliveCheck()
{
    // We don't need to stall normal connects and disconnects for keep-alive checking. We can do it later.
    std::unique_lock<std::mutex> lock(clients_by_fd_mutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;

    logger->logf(LOG_DEBUG, "Doing keep-alive check in thread %d", threadnr);

    try
    {
        auto it = clients_by_fd.begin();
        while (it != clients_by_fd.end())
        {
            Client_p &client = it->second;
            if (client && client->keepAliveExpired())
            {
                client->setDisconnectReason("Keep-alive expired: " + client->getKeepAliveInfoString());
                it = clients_by_fd.erase(it);
            }
            else
                it++;
        }
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error handling keep-alives: %s.", ex.what());
    }
}

void ThreadData::initAuthPlugin()
{
    authPlugin.loadPlugin(confFileParser.authPluginPath);
    authPlugin.init();
    authPlugin.securityInit(false);
}

void ThreadData::reload(GlobalSettings settings)
{
    logger->logf(LOG_DEBUG, "Doing reload in thread %d", threadnr);

    try
    {
        settingsLocalCopy = settings;

        authPlugin.securityCleanup(true);
        authPlugin.securityInit(true);
    }
    catch (AuthPluginException &ex)
    {
        logger->logf(LOG_ERR, "Error reloading auth plugin: %s. Security checks will now fail, because we don't know the status of the plugin anymore.", ex.what());
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error reloading: %s.", ex.what());
    }
}

void ThreadData::queueReload(GlobalSettings settings)
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::reload, this, settings);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::wakeUpThread()
{
    uint64_t one = 1;
    check<std::runtime_error>(write(taskEventFd, &one, sizeof(uint64_t)));
}





