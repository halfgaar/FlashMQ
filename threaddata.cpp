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

void ThreadData::removeClient(std::shared_ptr<Client> client)
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
    catch (AuthPluginException &ex)
    {
        logger->logf(LOG_ERR, "Error reloading auth plugin: %s. Security checks will now fail, because we don't know the status of the plugin anymore.", ex.what());
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error reloading: %s.", ex.what());
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





