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

#include "threaddata.h"
#include <string>
#include <sstream>
#include <cassert>

ThreadData::ThreadData(int threadnr, std::shared_ptr<SubscriptionStore> &subscriptionStore, std::shared_ptr<Settings> settings) :
    subscriptionStore(subscriptionStore),
    settingsLocalCopy(*settings.get()),
    authentication(settingsLocalCopy),
    threadnr(threadnr)
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
    std::string name = threadName.str();
    const char *c_str = name.c_str();
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
}

/**
 * @brief ThreadData::queuePublishStatsOnDollarTopic makes this thread publish the $SYS topics.
 * @param threads
 *
 * We want to do that in a thread because all authentication state is thread local.
 */
void ThreadData::queuePublishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads)
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::publishStatsOnDollarTopic, this, threads);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::publishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads)
{
    uint nrOfClients = 0;
    uint64_t receivedMessageCountPerSecond = 0;
    uint64_t receivedMessageCount = 0;
    uint64_t sentMessageCountPerSecond = 0;
    uint64_t sentMessageCount = 0;

    for (const std::shared_ptr<ThreadData> &thread : threads)
    {
        nrOfClients += thread->getNrOfClients();

        receivedMessageCountPerSecond += thread->getReceivedMessagePerSecond();
        receivedMessageCount += thread->getReceivedMessageCount();

        sentMessageCountPerSecond += thread->getSentMessagePerSecond();
        sentMessageCount += thread->getSentMessageCount();
    }

    publishStat("$SYS/broker/clients/total", nrOfClients);

    publishStat("$SYS/broker/load/messages/received/total", receivedMessageCount);
    publishStat("$SYS/broker/load/messages/received/persecond", receivedMessageCountPerSecond);

    publishStat("$SYS/broker/load/messages/sent/total", sentMessageCount);
    publishStat("$SYS/broker/load/messages/sent/persecond", sentMessageCountPerSecond);

    publishStat("$SYS/broker/retained messages/count", subscriptionStore->getRetainedMessageCount());

    publishStat("$SYS/broker/sessions/total", subscriptionStore->getSessionCount());

    publishStat("$SYS/broker/subscriptions/count", subscriptionStore->getSubscriptionCount());
}

void ThreadData::publishStat(const std::string &topic, uint64_t n)
{
    std::vector<std::string> subtopics;
    splitTopic(topic, subtopics);
    const std::string payload = std::to_string(n);
    Publish p(topic, payload, 0);
    MqttPacket pack(p);
    subscriptionStore->queuePacketAtSubscribers(subtopics, pack, true);
    subscriptionStore->setRetainedMessage(topic, subtopics, payload, 0);
}

void ThreadData::removeQueuedClients()
{
    std::vector<int> fds;
    fds.reserve(1024); // 1024 is arbitrary...

    {
        std::lock_guard<std::mutex> lck2(clientsToRemoveMutex);

        for (const std::weak_ptr<Client> &c : clientsQueuedForRemoving)
        {
            std::shared_ptr<Client> client = c.lock();
            if (client)
            {
                int fd = client->getFd();
                fds.push_back(fd);
            }
        }

        clientsQueuedForRemoving.clear();
    }

    {
        std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
        for(int fd : fds)
        {
            clients_by_fd.erase(fd);
        }
    }
}

void ThreadData::giveClient(std::shared_ptr<Client> client)
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

std::shared_ptr<Client> ThreadData::getClient(int fd)
{
    std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
    return this->clients_by_fd[fd];
}

void ThreadData::removeClientQueued(const std::shared_ptr<Client> &client)
{
    bool wakeUpNeeded = true;

    {
        std::lock_guard<std::mutex> locker(clientsToRemoveMutex);
        wakeUpNeeded = clientsQueuedForRemoving.empty();
        clientsQueuedForRemoving.push_front(client);
    }

    if (wakeUpNeeded)
    {
        auto f = std::bind(&ThreadData::removeQueuedClients, this);
        std::lock_guard<std::mutex> lockertaskQueue(taskQueueMutex);
        taskQueue.push_front(f);

        wakeUpThread();
    }
}

void ThreadData::removeClientQueued(int fd)
{
    bool wakeUpNeeded = true;
    std::shared_ptr<Client> clientFound;

    {
        std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
        auto client_it = this->clients_by_fd.find(fd);
        if (client_it != this->clients_by_fd.end())
        {
            clientFound = client_it->second;
        }
    }

    if (clientFound)
    {
        {
            std::lock_guard<std::mutex> locker(clientsToRemoveMutex);
            wakeUpNeeded = clientsQueuedForRemoving.empty();
            clientsQueuedForRemoving.push_front(clientFound);
        }

        if (wakeUpNeeded)
        {
            auto f = std::bind(&ThreadData::removeQueuedClients, this);
            std::lock_guard<std::mutex> lockertaskQueue(taskQueueMutex);
            taskQueue.push_front(f);

            wakeUpThread();
        }
    }
}

void ThreadData::removeClient(std::shared_ptr<Client> client)
{
    // This function is only for same-thread calling.
    assert(pthread_self() == thread.native_handle());

    client->markAsDisconnecting();

    std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
    clients_by_fd.erase(client->getFd());
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

void ThreadData::queueQuit()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::quit, this);
    taskQueue.push_front(f);

    authentication.setQuitting();

    wakeUpThread();
}

void ThreadData::waitForQuit()
{
    thread.join();
}

void ThreadData::queuePasswdFileReload()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&Authentication::loadMosquittoPasswordFile, &authentication);
    taskQueue.push_front(f);

    auto f2 = std::bind(&Authentication::loadMosquittoAclFile, &authentication);
    taskQueue.push_front(f2);

    wakeUpThread();
}

int ThreadData::getNrOfClients() const
{
    return clients_by_fd.size();
}

void ThreadData::incrementReceivedMessageCount()
{
    receivedMessageCount++;
}

uint64_t ThreadData::getReceivedMessageCount() const
{
    return receivedMessageCount;
}

/**
 * @brief ThreadData::getReceivedMessagePerSecond gets the amount of seconds received, averaged over the last time this was called.
 * @return
 *
 * Locking is not required, because the counter is not written to from here.
 */
uint64_t ThreadData::getReceivedMessagePerSecond()
{
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    std::chrono::milliseconds msSinceLastTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - receivedMessagePreviousTime);
    uint64_t messagesTimes1000 = (receivedMessageCount - receivedMessageCountPrevious) * 1000;
    uint64_t result = messagesTimes1000 / (msSinceLastTime.count() + 1); // branchless avoidance of div by 0;
    receivedMessagePreviousTime = now;
    receivedMessageCountPrevious = receivedMessageCount;
    return result;
}

void ThreadData::incrementSentMessageCount(uint64_t n)
{
    sentMessageCount += n;
}

uint64_t ThreadData::getSentMessageCount() const
{
    return sentMessageCount;
}

uint64_t ThreadData::getSentMessagePerSecond()
{
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    std::chrono::milliseconds msSinceLastTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - sentMessagePreviousTime);
    uint64_t messagesTimes1000 = (sentMessageCount - sentMessageCountPrevious) * 1000;
    uint64_t result = messagesTimes1000 / (msSinceLastTime.count() + 1); // branchless avoidance of div by 0;
    sentMessagePreviousTime = now;
    sentMessageCountPrevious = sentMessageCount;
    return result;
}

void ThreadData::queueAuthPluginPeriodicEvent()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::authPluginPeriodicEvent, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::authPluginPeriodicEvent()
{
    authentication.periodicEvent();
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
            std::shared_ptr<Client> &client = it->second;
            if (client && client->keepAliveExpired())
            {
                client->setDisconnectReason("Keep-alive expired: " + client->getKeepAliveInfoString());
                it = clients_by_fd.erase(it);
            }
            else
            {
                if (client)
                    client->resetBuffersIfEligible();
                it++;
            }
        }
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error handling keep-alives: %s.", ex.what());
    }
}

void ThreadData::initAuthPlugin()
{
    authentication.loadMosquittoPasswordFile();
    authentication.loadMosquittoAclFile();
    authentication.loadPlugin(settingsLocalCopy.authPluginPath);
    authentication.init();
    authentication.securityInit(false);
}

void ThreadData::cleanupAuthPlugin()
{
    authentication.cleanup();
}

void ThreadData::reload(std::shared_ptr<Settings> settings)
{
    logger->logf(LOG_DEBUG, "Doing reload in thread %d", threadnr);

    try
    {
        // Because the auth plugin has a reference to it, it will also be updated.
        settingsLocalCopy = *settings.get();

        authentication.securityCleanup(true);
        authentication.securityInit(true);
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error reloading auth plugin: %s. Security checks will now fail, because we don't know the status of the plugin anymore.", ex.what());
    }
}

void ThreadData::queueReload(std::shared_ptr<Settings> settings)
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





