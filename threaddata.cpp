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

#include "globalstats.h"

KeepAliveCheck::KeepAliveCheck(const std::shared_ptr<Client> client) :
    client(client)
{

}

AsyncAuth::AsyncAuth(std::weak_ptr<Client> client, AuthResult result, const std::string authMethod, const std::string &authData) :
    client(client),
    result(result),
    authMethod(authMethod),
    authData(authData)
{

}

ThreadData::ThreadData(int threadnr, const Settings &settings) :
    settingsLocalCopy(settings),
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

    /*
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
    */
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

void ThreadData::queueSendingQueuedWills()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::sendQueuedWills, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::queueRemoveExpiredSessions()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::removeExpiredSessions, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::queueRemoveExpiredRetainedMessages()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::removeExpiredRetainedMessages, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::queueClientNextKeepAliveCheck(std::shared_ptr<Client> &client, bool keepRechecking)
{
    const std::chrono::seconds k = client->getSecondsTillKillTime();

    if (k == std::chrono::seconds(0))
        return;

    const std::chrono::seconds when = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch() + k);

    KeepAliveCheck check(client);
    check.recheck = keepRechecking;
    queuedKeepAliveChecks[when].push_back(check);
}

void ThreadData::queueClientNextKeepAliveCheckLocked(std::shared_ptr<Client> &client, bool keepRechecking)
{
    std::lock_guard<std::mutex> locker(this->queuedKeepAliveMutex);
    queueClientNextKeepAliveCheck(client, keepRechecking);
}

/**
 * @brief ThreadData::continuationOfAuthentication is logic that either needs to be called synchronously, or by the a plugin.
 * @param client
 * @param authResult
 * @param authMethod
 * @param returnData
 *
 * It always needs to run in the client's thread. For that, also see queueContinuationOfAuthentication().
 */
void ThreadData::continuationOfAuthentication(std::shared_ptr<Client> &client, AuthResult authResult, const std::string &authMethod, const std::string &returnData)
{
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    assert(pthread_self() == thread.native_handle());
#endif

    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();

    if (authResult == AuthResult::auth_continue)
    {
        Auth auth(ReasonCodes::ContinueAuthentication, authMethod, returnData);
        MqttPacket pack(auth);
        client->writeMqttPacket(pack);
    }
    else if (authResult == AuthResult::success)
    {
        if (!client->getAuthenticated()) // First auth sends connack packets on success.
        {
            if (!returnData.empty())
                client->addAuthReturnDataToStagedConnAck(returnData);

            client->sendConnackSuccess();
            subscriptionStore->registerClientAndKickExistingOne(client);
        }
        else // Reauth (to authenticated clients) sends AUTH on success.
        {
            Auth auth(ReasonCodes::Success, authMethod, returnData);
            MqttPacket authPack(auth);
            client->writeMqttPacket(authPack);
            logger->logf(LOG_NOTICE, "Client '%s', user '%s' reauthentication successful.", client->getClientId().c_str(), client->getUsername().c_str());
        }
    }
    else
    {
        if (!client->getAuthenticated()) // First auth sends connack with 'deny' code packets on failure.
        {
            const ReasonCodes reason = authResultToReasonCode(authResult);
            client->sendConnackDeny(reason);
        }
        else  // Reauth (to authenticated clients) sends DISCONNECT on failure.
        {
            const ReasonCodes finalResult = authResultToReasonCode(authResult);
            Disconnect disconnect(client->getProtocolVersion(), finalResult);
            MqttPacket disconnectPack(disconnect);
            client->setDisconnectReason("Reauth denied");
            client->setReadyForDisconnect();
            client->writeMqttPacket(disconnectPack);
            logger->logf(LOG_NOTICE, "Client '%s', user '%s' reauthentication denied.", client->getClientId().c_str(), client->getUsername().c_str());
        }
    }
}

void ThreadData::continueAsyncAuths()
{
    assert(pthread_self() == thread.native_handle());

    std::forward_list<AsyncAuth> asyncClientsReadyCopies;

    {
        std::lock_guard<std::mutex> lck2(asyncClientsReadyMutex);
        asyncClientsReadyCopies = this->asyncClientsReady;
        asyncClientsReady.clear();
    }

    for(AsyncAuth &auth : asyncClientsReadyCopies)
    {
        std::shared_ptr<Client> c = auth.client.lock();

        if (!c)
            continue;

        this->continuationOfAuthentication(c, auth.result, auth.authMethod, auth.authData);
    }
}

void ThreadData::clientDisconnectEvent(const std::string &clientid)
{
    authentication.clientDisconnected(clientid);
}

void ThreadData::queueContinuationOfAuthentication(const std::shared_ptr<Client> &client, AuthResult authResult, const std::string &authMethod, const std::string &returnData)
{
    bool wakeUpNeeded = true;

    {
        std::lock_guard<std::mutex> locker(asyncClientsReadyMutex);
        wakeUpNeeded = asyncClientsReady.empty();
        asyncClientsReady.emplace_front(client, authResult, authMethod, returnData);
    }

    if (wakeUpNeeded)
    {
        auto f = std::bind(&ThreadData::continueAsyncAuths, this);
        std::lock_guard<std::mutex> lockertaskQueue(taskQueueMutex);
        taskQueue.push_front(f);

        wakeUpThread();
    }
}

void ThreadData::queueClientDisconnectEvent(const std::string &clientid)
{
    auto f = std::bind(&ThreadData::clientDisconnectEvent, this, clientid);
    std::lock_guard<std::mutex> lockertaskQueue(taskQueueMutex);
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

    uint64_t mqttConnectCountPerSecond = 0;
    uint64_t mqttConnectCount = 0;

    for (const std::shared_ptr<ThreadData> &thread : threads)
    {
        nrOfClients += thread->getNrOfClients();

        receivedMessageCountPerSecond += thread->receivedMessageCounter.getPerSecond();
        receivedMessageCount += thread->receivedMessageCounter.get();

        sentMessageCountPerSecond += thread->sentMessageCounter.getPerSecond();
        sentMessageCount += thread->sentMessageCounter.get();

        mqttConnectCountPerSecond += thread->mqttConnectCounter.getPerSecond();
        mqttConnectCount += thread->mqttConnectCounter.get();
    }

    GlobalStats *globalStats = GlobalStats::getInstance();

    publishStat("$SYS/broker/network/socketconnects/total", globalStats->socketConnects.get());
    publishStat("$SYS/broker/network/socketconnects/persecond", globalStats->socketConnects.getPerSecond());

    publishStat("$SYS/broker/clients/mqttconnects/total", mqttConnectCount);
    publishStat("$SYS/broker/clients/mqttconnects/persecond", mqttConnectCountPerSecond);

    publishStat("$SYS/broker/clients/total", nrOfClients);

    publishStat("$SYS/broker/load/messages/received/total", receivedMessageCount);
    publishStat("$SYS/broker/load/messages/received/persecond", receivedMessageCountPerSecond);

    publishStat("$SYS/broker/load/messages/sent/total", sentMessageCount);
    publishStat("$SYS/broker/load/messages/sent/persecond", sentMessageCountPerSecond);

    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();

    publishStat("$SYS/broker/retained messages/count", subscriptionStore->getRetainedMessageCount());

    publishStat("$SYS/broker/sessions/total", subscriptionStore->getSessionCount());

    publishStat("$SYS/broker/subscriptions/count", subscriptionStore->getSubscriptionCount());
}

void ThreadData::publishStat(const std::string &topic, uint64_t n)
{
    const std::string payload = std::to_string(n);
    Publish p(topic, payload, 0);
    PublishCopyFactory factory(&p);
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    subscriptionStore->queuePacketAtSubscribers(factory, true);
    subscriptionStore->setRetainedMessage(p, factory.getSubtopics());
}

/**
 * @brief ThreadData::sendQueuedWills is not an operation per thread, but it's good practice to perform certain tasks in the worker threads, where
 * the thread-local globals work.
 */
void ThreadData::sendQueuedWills()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    subscriptionStore->sendQueuedWillMessages();
}

/**
 * @brief ThreadData::removeExpiredSessions is not an operation per thread, but it's good practice to perform certain tasks in the worker threads, where
 * the thread-local globals work.
 */
void ThreadData::removeExpiredSessions()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    subscriptionStore->removeExpiredSessionsClients();
}

/**
 * @brief ThreadData::removeExpiredRetainedMessages is not an operation per thread, but it's good practice to perform certain tasks in the worker threads, where
 * the thread-local globals work.
 */
void ThreadData::removeExpiredRetainedMessages()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    subscriptionStore->expireRetainedMessages();
}

void ThreadData::sendAllWills()
{
    std::lock_guard<std::mutex> lck(clients_by_fd_mutex);

    for(auto &pair : clients_by_fd)
    {
        std::shared_ptr<Client> &c = pair.second;
        c->sendOrQueueWill();
    }

    allWillsQueued = true;
}

void ThreadData::sendAllDisconnects()
{
    std::vector<std::shared_ptr<Client>> clientsFound;

    {
        std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
        clientsFound.reserve(clients_by_fd.size());

        for(auto &pair : clients_by_fd)
        {
            clientsFound.push_back(pair.second);
        }
    }

    for (std::shared_ptr<Client> &c : clientsFound)
    {
        c->serverInitiatedDisconnect(ReasonCodes::ServerShuttingDown);
    }

    allDisconnectsSent = true;
}

void ThreadData::removeQueuedClients()
{
    // Using shared pointers to have a claiming reference in case we lose the clients between the two locks.
    std::vector<std::shared_ptr<Client>> clients;

    {
        std::lock_guard<std::mutex> lck2(clientsToRemoveMutex);

        for (const std::weak_ptr<Client> &c : clientsQueuedForRemoving)
        {
            std::shared_ptr<Client> client = c.lock();
            if (client)
            {
                clients.push_back(client);
            }
        }

        clientsQueuedForRemoving.clear();
    }

    {
        std::lock_guard<std::mutex> lck(clients_by_fd_mutex);
        for(const std::shared_ptr<Client> &client : clients)
        {
            int fd = client->getFd();
            clients_by_fd.erase(fd);
        }
    }
}

void ThreadData::giveClient(std::shared_ptr<Client> client)
{
    const int fd = client->getFd();

    {
        std::lock_guard<std::mutex> locker(clients_by_fd_mutex);
        clients_by_fd[fd] = client;
    }

    queueClientNextKeepAliveCheckLocked(client, false);

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev));
}

std::shared_ptr<Client> ThreadData::getClient(int fd)
{
    std::lock_guard<std::mutex> lck(clients_by_fd_mutex);

    auto pos = clients_by_fd.find(fd);

    if (pos == clients_by_fd.end())
        return std::shared_ptr<Client>();

    return pos->second;
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

void ThreadData::queuepluginPeriodicEvent()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::pluginPeriodicEvent, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::pluginPeriodicEvent()
{
    authentication.periodicEvent();
}

void ThreadData::queueSendWills()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::sendAllWills, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::queueSendDisconnects()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::sendAllDisconnects, this);
    taskQueue.push_front(f);

    wakeUpThread();
}

void ThreadData::doKeepAliveCheck()
{
    logger->logf(LOG_DEBUG, "doKeepAliveCheck in thread %d", threadnr);

    const std::chrono::seconds now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch());

    try
    {
        // Put clients to delete in here, to avoid holding two locks.
        std::vector<std::shared_ptr<Client>> clientsToRemove;

        std::vector<std::shared_ptr<Client>> clientsToRecheck;

        const int slotsTotal = this->queuedKeepAliveChecks.size();
        int slotsProcessed = 0;
        int clientsChecked = 0;

        {
            logger->logf(LOG_DEBUG, "Checking clients with pending keep-alive checks in thread %d", threadnr);

            std::lock_guard<std::mutex> locker(this->queuedKeepAliveMutex);

            auto pos = this->queuedKeepAliveChecks.begin();
            while (pos != this->queuedKeepAliveChecks.end())
            {
                const std::chrono::seconds &doCheckAt = pos->first;

                if (doCheckAt > now)
                    break;

                slotsProcessed++;

                std::vector<KeepAliveCheck> &checks = pos->second;

                for (KeepAliveCheck &k : checks)
                {
                    std::shared_ptr<Client> client = k.client.lock();
                    if (client)
                    {
                        clientsChecked++;

                        if (client->keepAliveExpired())
                        {
                            clientsToRemove.push_back(client);
                        }
                        else if (k.recheck)
                        {
                            clientsToRecheck.push_back(client);
                        }
                    }
                }

                pos = this->queuedKeepAliveChecks.erase(pos);
            }

            for (std::shared_ptr<Client> &c : clientsToRecheck)
            {
                c->resetBuffersIfEligible();
                queueClientNextKeepAliveCheck(c, true);
            }
        }

        logger->logf(LOG_DEBUG, "Checked %d clients in %d of %d keep-alive slots in thread %d", clientsChecked, slotsProcessed, slotsTotal, threadnr);

        {
            std::unique_lock<std::mutex> lock(clients_by_fd_mutex);

            for (std::shared_ptr<Client> c : clientsToRemove)
            {
                c->setDisconnectReason("Keep-alive expired: " + c->getKeepAliveInfoString());
                clients_by_fd.erase(c->getFd());
            }
        }
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error handling keep-alives: %s.", ex.what());
    }
}

void ThreadData::initplugin()
{
    authentication.loadMosquittoPasswordFile();
    authentication.loadMosquittoAclFile();
    authentication.loadPlugin(settingsLocalCopy.pluginPath);
    authentication.init();
    authentication.securityInit(false);
}

void ThreadData::cleanupplugin()
{
    authentication.cleanup();
}

void ThreadData::reload(const Settings &settings)
{
    logger->logf(LOG_DEBUG, "Doing reload in thread %d", threadnr);

    try
    {
        // Because the auth plugin has a reference to it, it will also be updated.
        settingsLocalCopy = settings;

        authentication.securityCleanup(true);
        authentication.securityInit(true);
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error reloading auth plugin: %s. Security checks will now fail, because we don't know the status of the plugin anymore.", ex.what());
    }
}

void ThreadData::queueReload(const Settings &settings)
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





