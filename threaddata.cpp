/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "threaddata.h"
#include <string>
#include <sstream>
#include <cassert>

#include "globalstats.h"
#include "subscriptionstore.h"
#include "mainapp.h"
#include "utils.h"

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

ThreadData::ThreadData(int threadnr, const Settings &settings, const PluginLoader &pluginLoader) :
    pluginLoader(pluginLoader),
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
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::queueSendingQueuedWills()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::sendQueuedWills, this);
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::queueRemoveExpiredSessions()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::removeExpiredSessions, this);
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::queuePurgeSubscriptionTree()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    if (subscriptionStore->hasDeferredSubscriptionTreeNodesForPurging())
        return;

    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::purgeSubscriptionTree, this);
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::queueRemoveExpiredRetainedMessages()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    if (subscriptionStore->hasDeferredRetainedMessageNodesForPurging())
        return;

    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::removeExpiredRetainedMessages, this);
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::queueClientNextKeepAliveCheck(std::shared_ptr<Client> &client, bool keepRechecking)
{
    const std::chrono::seconds k = client->getSecondsTillKeepAliveAction();

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
    assert(pthread_self() == thread.native_handle());

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

            const std::shared_ptr<WillPublish> will = client->getStagedWill();

            if (will && authentication.aclCheck(*will, will->payload, AclAccess::register_will) == AuthResult::success)
            {
                client->setWillFromStaged();
            }

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
        asyncClientsReadyCopies = std::move(this->asyncClientsReady);
        this->asyncClientsReady.clear();
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

void ThreadData::bridgeReconnect()
{
    std::lock_guard<std::mutex> locker(clients_by_fd_mutex);

    bool requeue = false;

    for (std::shared_ptr<BridgeState> &bridge : bridges)
    {
        try
        {
            bridge->initSSL(false);

            std::shared_ptr<Client> client;
            std::shared_ptr<Session> session = bridge->session.lock();

            if (session)
                client = session->makeSharedClient();

            if (client)
                continue;

            if (!bridge->timeForNewReconnectAttempt())
            {
                continue;
            }

            std::shared_ptr<ThreadData> _threadData = bridge->threadData.lock();

            if (!_threadData)
                continue;

            _threadData->publishBridgeState(bridge, false);

            if (bridge->dnsResults.empty())
            {
                // If no DNS query is pending, queue one.
                if (bridge->dns.idle())
                {
                    bridge->dns.query(bridge->c.address, bridge->c.inet_protocol, std::chrono::milliseconds(5000));
                    requeue = true;
                    continue;
                }

                const std::list<FMQSockaddr_in6> &results = bridge->dns.getResult();

                // If empty, we're still waiting for the result but there is no error.
                if (results.empty())
                {
                    requeue = true;
                    continue;
                }

                bridge->dnsResults = results;
            }

            FMQSockaddr_in6 addr = bridge->popDnsResult();

            bridge->registerReconnect();

            int sockfd = check<std::runtime_error>(socket(addr.getFamily(), SOCK_STREAM, 0));
            int flags = fcntl(sockfd, F_GETFL);
            fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

            SSL *clientSSL = nullptr;
            if (bridge->c.tlsMode > BridgeTLSMode::None)
            {
                clientSSL = SSL_new(bridge->sslctx->get());

                if (clientSSL == NULL)
                {
                    logger->logf(LOG_ERR, "Problem creating SSL object for bridge. Closing client.");
                    close(sockfd);
                    continue;
                }

                SSL_set_fd(clientSSL, sockfd);
            }

            std::shared_ptr<Client> c(new Client(sockfd, _threadData, clientSSL, false, false, nullptr, settingsLocalCopy));
            c->setBridgeState(bridge);

            logger->logf(LOG_NOTICE, "Connecting brige: %s", c->repr().c_str());

            clients_by_fd[sockfd] = c;

            struct epoll_event ev;
            memset(&ev, 0, sizeof (struct epoll_event));
            ev.data.fd = sockfd;
            ev.events = EPOLLIN | EPOLLOUT;
            check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev));

            // Perform one keep-alive check, for the pre-auth stage. The repeating once are done in processing the connack.
            queueClientNextKeepAliveCheckLocked(c, false);

            if (session)
            {
                session->assignActiveConnection(c);
                c->assignSession(session);
            }
            else
            {
                std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
                bridge->session = subscriptionStore->getBridgeSession(c);
            }

            c->connectToBridgeTarget(addr);
        }
        catch (std::exception &ex)
        {
            logger->log(LOG_ERR) << "Error creating bridge '" << bridge->c.clientidPrefix << "': " << ex.what();
            bridge->registerReconnect();
        }
    }

    if (requeue)
    {
        auto f = std::bind(&ThreadData::bridgeReconnect, this);
        delayedTasks.addTask(f, 500);
    }
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
        taskQueue.push_back(f);

        wakeUpThread();
    }
}

void ThreadData::clientDisconnectActions(bool authenticated, const std::string &clientid, std::shared_ptr<WillPublish> &willPublish, std::shared_ptr<Session> &session,
                                         std::weak_ptr<BridgeState> &bridgeState)
{
    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();

    assert(store);

    publishBridgeState(bridgeState.lock(), false);

    if (willPublish)
    {
        store->queueWillMessage(willPublish, clientid, session);
    }

    if (session && session->getDestroyOnDisconnect())
    {
        store->removeSession(session);
    }
    else
    {
        store->queueSessionRemoval(session);
    }

    if (authenticated)
        clientDisconnectEvent(clientid);
}

void ThreadData::queueClientDisconnectActions(bool authenticated, const std::string &clientid, std::shared_ptr<WillPublish> &&willPublish, std::shared_ptr<Session> &&session,
                                              std::weak_ptr<BridgeState> &&bridgeState)
{
    auto f = std::bind(&ThreadData::clientDisconnectActions, this, authenticated, clientid, std::move(willPublish), std::move(session), std::move(bridgeState));
    assert(!willPublish);
    assert(!session);
    std::lock_guard<std::mutex> lockertaskQueue(taskQueueMutex);
    taskQueue.push_back(std::move(f));

    wakeUpThread();
}

void ThreadData::queueBridgeReconnect()
{
    auto f = std::bind(&ThreadData::bridgeReconnect, this);

    {
        std::lock_guard<std::mutex> lockertaskQueue(taskQueueMutex);
        taskQueue.push_back(f);
    }

    wakeUpThread();
}

void ThreadData::publishStatsOnDollarTopic(std::vector<std::shared_ptr<ThreadData>> &threads)
{
    uint nrOfClients = 0;
    double receivedMessageCountPerSecond = 0;
    uint64_t receivedMessageCount = 0;
    double sentMessageCountPerSecond = 0;
    uint64_t sentMessageCount = 0;

    double mqttConnectCountPerSecond = 0;
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

    for (auto &pair : globalStats->getExtras())
    {
        Publish p(pair.first, pair.second, 0);
        publishWithAcl(p);
    }
}

void ThreadData::publishStat(const std::string &topic, uint64_t n)
{
    const std::string payload = std::to_string(n);
    Publish p(topic, payload, 0);
    publishWithAcl(p, true);
}

void ThreadData::publishBridgeState(std::shared_ptr<BridgeState> bridge, bool connected)
{
    if (!bridge)
        return;

    const std::string payload = connected ? "1" : "0";

    std::stringstream ss;
    ss << "$SYS/broker/bridge/" << bridge->c.clientidPrefix << "/connected";
    const std::string topic = ss.str();

    GlobalStats *globalStats = GlobalStats::getInstance();
    globalStats->setExtra(topic, payload);

    Publish p(topic, payload, 0);
    publishWithAcl(p, true);
}

void ThreadData::publishWithAcl(Publish &pub, bool setRetain)
{
    authentication.aclCheck(pub, pub.payload, AclAccess::write);

    PublishCopyFactory factory(&pub);
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    subscriptionStore->queuePacketAtSubscribers(factory, "", true);

    if (setRetain)
        subscriptionStore->setRetainedMessage(pub, factory.getSubtopics());
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

void ThreadData::purgeSubscriptionTree()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    bool done = subscriptionStore->purgeSubscriptionTree();

    if (!done)
    {
        auto f = std::bind(&ThreadData::purgeSubscriptionTree, this);
        addDelayedTask(f, 100);
    }
}

/**
 * @brief ThreadData::removeExpiredRetainedMessages is not an operation per thread, but it's good practice to perform certain tasks in the worker threads, where
 * the thread-local globals work.
 */
void ThreadData::removeExpiredRetainedMessages()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = MainApp::getMainApp()->getSubscriptionStore();
    bool done = subscriptionStore->expireRetainedMessages();

    if (!done)
    {
        auto f = std::bind(&ThreadData::removeExpiredRetainedMessages, this);
        addDelayedTask(f, 100);
    }
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

void ThreadData::giveClient(std::shared_ptr<Client> &&client)
{
    const int fd = client->getFd();

    queueClientNextKeepAliveCheckLocked(client, false);

    {
        std::lock_guard<std::mutex> locker(clients_by_fd_mutex);
        clients_by_fd[fd] = std::move(client); // We must give up ownership here, to avoid calling the client destructor in the main thread.
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev));
}

void ThreadData::giveBridge(std::shared_ptr<BridgeState> &bridgeConfig)
{
    {
        std::lock_guard<std::mutex> locker(clients_by_fd_mutex);
        bridges.push_back(bridgeConfig);
    }
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
        taskQueue.push_back(f);

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
            taskQueue.push_back(f);

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
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::queueQuit()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::quit, this);
    taskQueue.push_back(f);

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
    taskQueue.push_back(f);

    auto f2 = std::bind(&Authentication::loadMosquittoAclFile, &authentication);
    taskQueue.push_back(f2);

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
    taskQueue.push_back(f);

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
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::queueSendDisconnects()
{
    std::lock_guard<std::mutex> locker(taskQueueMutex);

    auto f = std::bind(&ThreadData::sendAllDisconnects, this);
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::pollExternalFd(int fd, uint32_t events, const std::weak_ptr<void> &p)
{
    int mode = EPOLL_CTL_MOD;
    auto pos = externalFds.find(fd);
    if (pos == externalFds.end())
    {
        mode = EPOLL_CTL_ADD;
    }

    if (mode == EPOLL_CTL_ADD || !p.expired())
        externalFds[fd] = p;

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = events;
    check<std::runtime_error>(epoll_ctl(this->epollfd, mode, fd, &ev));
}

void ThreadData::pollExternalRemove(int fd)
{
    this->externalFds.erase(fd);
    if (epoll_ctl(this->epollfd, EPOLL_CTL_DEL, fd, NULL) != 0)
    {
        Logger *logger = Logger::getInstance();
        logger->logf(LOG_ERR, "Removing externally watched fd %d from epoll produced error: %s", fd, strerror(errno));
    }
}

uint32_t ThreadData::addDelayedTask(std::function<void ()> f, uint32_t delayMs)
{
    return delayedTasks.addTask(f, delayMs);
}

void ThreadData::removeDelayedTask(uint32_t id)
{
    delayedTasks.eraseTask(id);
}

void ThreadData::addImmediateTask(std::function<void ()> f)
{
    bool wakeupNeeded = true;

    {
        std::lock_guard<std::mutex> lockertaskQueue(taskQueueMutex);
        wakeupNeeded = taskQueue.empty();
        taskQueue.push_back(f);
    }

    if (wakeupNeeded)
    {
        wakeUpThread();
    }
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

                std::vector<KeepAliveCheck> const &checks = pos->second;

                for (KeepAliveCheck const &k : checks)
                {
                    std::shared_ptr<Client> client = k.client.lock();
                    if (client)
                    {
                        clientsChecked++;

                        if (client->isOutgoingConnection())
                        {
                            client->writePing();
                        }

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
    authentication.loadPlugin(pluginLoader);
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

        for (auto b : this->bridges)
        {
            b->initSSL(true);
        }

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
    taskQueue.push_back(f);

    wakeUpThread();
}

void ThreadData::wakeUpThread()
{
    uint64_t one = 1;
    check<std::runtime_error>(write(taskEventFd, &one, sizeof(uint64_t)));
}





