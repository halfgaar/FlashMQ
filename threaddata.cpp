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
#include "threadloop.h"

KeepAliveCheck::KeepAliveCheck(const std::shared_ptr<Client> client) :
    client(client)
{

}

QueuedRetainedMessage::QueuedRetainedMessage(const Publish &p, const std::vector<std::string> &subtopics, const std::chrono::time_point<std::chrono::steady_clock> limit) :
    p(p),
    subtopics(subtopics),
    limit(limit)
{

}

ThreadData::ThreadData(int threadnr, const Settings &settings, const std::shared_ptr<PluginLoader> &pluginLoader) :
    epollfd(check<std::runtime_error>(epoll_create(999))),
    pluginLoader(pluginLoader),
    settingsLocalCopy(settings),
    authentication(settingsLocalCopy),
    threadnr(threadnr)
{
    logger = Logger::getInstance();

    taskEventFd = eventfd(0, EFD_NONBLOCK);
    if (taskEventFd < 0)
        throw std::runtime_error("Can't create eventfd.");

    disconnectingAllEventFd = eventfd(0, EFD_NONBLOCK);
    if (disconnectingAllEventFd < 0)
        throw std::runtime_error("Can't create eventfd.");

    randomish.seed(get_random_int<unsigned long>());

    struct epoll_event ev;
    std::array<int, 2> event_fds {taskEventFd, disconnectingAllEventFd};
    for (int efd : event_fds)
    {
        memset(&ev, 0, sizeof (struct epoll_event));
        ev.data.fd = efd;
        ev.events = EPOLLIN;
        check<std::runtime_error>(epoll_ctl(this->epollfd.get(), EPOLL_CTL_ADD, efd, &ev));
    }
}

ThreadData::~ThreadData()
{
    if (taskEventFd >= 0)
        close(taskEventFd);

    if (disconnectingAllEventFd >= 0)
    {
        close(disconnectingAllEventFd);
        disconnectingAllEventFd = -1;
    }

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
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::publishStatsOnDollarTopic, this, threads);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::queueSendingQueuedWills()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::sendQueuedWills, this);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::queueRemoveExpiredSessions()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::removeExpiredSessions, this);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::queuePurgeSubscriptionTree()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;
    if (subscriptionStore->hasDeferredSubscriptionTreeNodesForPurging())
        return;

    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::purgeSubscriptionTree, this);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::queueRemoveExpiredRetainedMessages()
{
    std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;
    if (subscriptionStore->hasDeferredRetainedMessageNodesForPurging())
        return;

    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::removeExpiredRetainedMessages, this);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::queueClientNextKeepAliveCheck(
    std::shared_ptr<Client> &client, bool keepRechecking,
    MutexLocked<std::map<std::chrono::seconds, std::vector<KeepAliveCheck>>> &queued_checks_locked)
{
    const std::chrono::seconds k = client->getSecondsTillKeepAliveAction();

    if (k == std::chrono::seconds(0))
        return;

    const std::chrono::seconds when = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch() + k);

    KeepAliveCheck check(client);
    check.recheck = keepRechecking;
    queued_checks_locked->operator[](when).push_back(check);
}

void ThreadData::queueClientNextKeepAliveCheck(std::shared_ptr<Client> &client, bool keepRechecking)
{
    auto queued_checks_locked = queuedKeepAliveChecks.lock();
    queueClientNextKeepAliveCheck(client, keepRechecking, queued_checks_locked);
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
    assert(pthread_self() == thread_id);

    std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;

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

            subscriptionStore->registerClientAndKickExistingOne(client);
            client->sendConnackSuccess();
            client->setAuthenticated(true);
            client->getSession()->sendAllPendingQosData();
            client->handleAfterAsyncQueue(client);
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
            client->setDisconnectStage(DisconnectStage::SendPendingAppData);
            client->writeMqttPacket(disconnectPack);
            logger->logf(LOG_NOTICE, "Client '%s', user '%s' reauthentication denied.", client->getClientId().c_str(), client->getUsername().c_str());
        }
    }
}

void ThreadData::clientDisconnectEvent(const std::string &clientid)
{
    authentication.clientDisconnected(clientid);
}

void ThreadData::bridgeReconnect()
{
    auto clients_locked = clients.lock();

    bool requeue = false;
    std::shared_ptr<BridgeState> bridge;
    std::shared_ptr<ThreadData> _threadData;

    for (auto &pair : clients_locked->bridges)
    {
        bridge = pair.second;

        if (!bridge)
            continue;

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

            _threadData = bridge->threadData.lock();

            if (!_threadData)
                continue;

            _threadData->publishBridgeState(bridge, false, "Connecting");

            if (bridge->dnsResults.empty())
            {
                // If no DNS query is pending, queue one.
                if (bridge->dns.idle())
                {
                    bridge->dns.query(bridge->c.address, bridge->c.inet_protocol, std::chrono::milliseconds(5000));
                    requeue = true;
                    continue;
                }

                const std::list<FMQSockaddr> &results = bridge->dns.getResult();

                // If empty, we're still waiting for the result but there is no error.
                if (results.empty())
                {
                    requeue = true;
                    continue;
                }

                bridge->dnsResults = results;
            }

            FMQSockaddr addr = bridge->popDnsResult();

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

            std::shared_ptr<Client> c(new Client(ClientType::LocalBridge, sockfd, _threadData, clientSSL, ConnectionProtocol::Mqtt, false, addr.getSockaddr(), settingsLocalCopy));
            c->setBridgeState(bridge);

            logger->logf(LOG_NOTICE, "Connecting brige: %s", c->repr().c_str());

            clients_locked->by_fd[sockfd] = c;

            struct epoll_event ev;
            memset(&ev, 0, sizeof (struct epoll_event));
            ev.data.fd = sockfd;
            ev.events = EPOLLIN | EPOLLOUT;
            check<std::runtime_error>(epoll_ctl(epollfd.get(), EPOLL_CTL_ADD, sockfd, &ev));

            queueClientNextKeepAliveCheck(c, true);

            if (session)
            {
                session->assignActiveConnection(c);
                c->assignSession(session);
            }
            else
            {
                std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;
                session = subscriptionStore->getBridgeSession(c);
                bridge->session = session;
            }

            session->setLocalPrefix(bridge->c.local_prefix);
            session->setRemotePrefix(bridge->c.remote_prefix);

            c->connectToBridgeTarget(addr);
        }
        catch (std::exception &ex)
        {
            logger->log(LOG_ERR) << "Error creating bridge '" << bridge->c.clientidPrefix << "': " << ex.what();
            bridge->registerReconnect();

            if (_threadData && bridge)
                _threadData->publishBridgeState(bridge, false, ex.what());
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
    auto f = [client, authResult, authMethod, returnData]
    {
        client->setAsyncAuthResult({authResult, authMethod, returnData});
    };

    bool wake_up_needed = false;

    {
        auto task_queue_locked = taskQueue.lock();
        wake_up_needed = task_queue_locked->empty();
        task_queue_locked->push_back(std::move(f));
    }

    if (wake_up_needed)
        wakeUpThread();
}

void ThreadData::clientDisconnectActions(
        bool authenticated, const std::string &clientid, std::shared_ptr<WillPublish> &willPublish, std::shared_ptr<Session> &session,
        std::weak_ptr<BridgeState> &bridgeState, const std::string &disconnect_reason)
{
    std::shared_ptr<SubscriptionStore> store = globals->subscriptionStore;

    assert(store);

    publishBridgeState(bridgeState.lock(), false, disconnect_reason);

    if (willPublish)
    {
        store->queueOrSendWillMessage(willPublish, session);
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

void ThreadData::queueClientDisconnectActions(
        bool authenticated, const std::string &clientid, std::shared_ptr<WillPublish> &&willPublish, std::shared_ptr<Session> &&session,
        std::weak_ptr<BridgeState> &&bridgeState, const std::string &disconnect_reason)
{
    auto f = std::bind(
                &ThreadData::clientDisconnectActions, this, authenticated, clientid, std::move(willPublish),
                std::move(session), std::move(bridgeState), disconnect_reason);
    assert(!willPublish);
    assert(!session);
    auto task_queue_locked = taskQueue.lock();
    task_queue_locked->push_back(std::move(f));

    wakeUpThread();
}

void ThreadData::queueBridgeReconnect()
{
    auto f = std::bind(&ThreadData::bridgeReconnect, this);

    {
        auto task_queue_locked = taskQueue.lock();
        task_queue_locked->push_back(f);
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

    double aclReadChecksPerSecond = 0;
    uint64_t aclReadCheckCount = 0;

    double aclWriteChecksPerSecond = 0;
    uint64_t aclWriteCheckCount = 0;

    double aclSubscribeChecksPerSecond = 0;
    uint64_t aclSubscribeCheckCount = 0;

    double aclRegisterWillChecksPerSecond = 0;
    uint64_t aclRegisterWillCheckCount = 0;

    double retainedMessagesSetPerSecond = 0;
    uint64_t retainedMessagesSetCount = 0;

    for (const std::shared_ptr<ThreadData> &thread : threads)
    {
        nrOfClients += thread->getNrOfClients();

        receivedMessageCountPerSecond += thread->receivedMessageCounter.getPerSecond();
        receivedMessageCount += thread->receivedMessageCounter.get();

        sentMessageCountPerSecond += thread->sentMessageCounter.getPerSecond();
        sentMessageCount += thread->sentMessageCounter.get();

        mqttConnectCountPerSecond += thread->mqttConnectCounter.getPerSecond();
        mqttConnectCount += thread->mqttConnectCounter.get();

        aclReadChecksPerSecond += thread->aclReadChecks.getPerSecond();
        aclReadCheckCount += thread->aclReadChecks.get();

        aclWriteChecksPerSecond += thread->aclWriteChecks.getPerSecond();
        aclWriteCheckCount += thread->aclWriteChecks.get();

        aclSubscribeChecksPerSecond += thread->aclSubscribeChecks.getPerSecond();
        aclSubscribeCheckCount += thread->aclSubscribeChecks.get();

        aclRegisterWillChecksPerSecond += thread->aclRegisterWillChecks.getPerSecond();
        aclRegisterWillCheckCount += thread->aclRegisterWillChecks.get();

        retainedMessagesSetPerSecond += thread->retainedMessageSet.getPerSecond();
        retainedMessagesSetCount += thread->retainedMessageSet.get();

        publishStat("$SYS/broker/threads/" + std::to_string(thread->threadnr) + "/drift/latest__ms", thread->driftCounter.getDrift().count());
        publishStat("$SYS/broker/threads/" + std::to_string(thread->threadnr) + "/drift/moving_avg__ms", thread->driftCounter.getAvgDrift().count());

        publishStat("$SYS/broker/threads/" + std::to_string(thread->threadnr) + "/retained_deferrals/count", thread->deferredRetainedMessagesSet.get());
        publishStat("$SYS/broker/threads/" + std::to_string(thread->threadnr) + "/retained_deferrals/persecond", thread->deferredRetainedMessagesSet.getPerSecond());
        publishStat("$SYS/broker/threads/" + std::to_string(thread->threadnr) + "/retained_deferrals/timeout/count", thread->deferredRetainedMessagesSetTimeout.get());
        publishStat("$SYS/broker/threads/" + std::to_string(thread->threadnr) + "/retained_deferrals/timeout/persecond", thread->deferredRetainedMessagesSetTimeout.getPerSecond());
    }

    publishStat("$SYS/broker/network/socketconnects/total", globals->stats.socketConnects.get());
    publishStat("$SYS/broker/network/socketconnects/persecond", globals->stats.socketConnects.getPerSecond());

    publishStat("$SYS/broker/clients/mqttconnects/total", mqttConnectCount);
    publishStat("$SYS/broker/clients/mqttconnects/persecond", mqttConnectCountPerSecond);

    publishStat("$SYS/broker/clients/total", nrOfClients);

    publishStat("$SYS/broker/load/messages/received/total", receivedMessageCount);
    publishStat("$SYS/broker/load/messages/received/persecond", receivedMessageCountPerSecond);

    publishStat("$SYS/broker/load/messages/sent/total", sentMessageCount);
    publishStat("$SYS/broker/load/messages/sent/persecond", sentMessageCountPerSecond);

    publishStat("$SYS/broker/load/messages/set_retained/total", retainedMessagesSetCount);
    publishStat("$SYS/broker/load/messages/set_retained/persecond", retainedMessagesSetPerSecond);

    publishStat("$SYS/broker/load/aclchecks/read/total", aclReadCheckCount);
    publishStat("$SYS/broker/load/aclchecks/read/persecond", aclReadChecksPerSecond);

    publishStat("$SYS/broker/load/aclchecks/write/total", aclWriteCheckCount);
    publishStat("$SYS/broker/load/aclchecks/write/persecond", aclWriteChecksPerSecond);

    publishStat("$SYS/broker/load/aclchecks/subscribe/total", aclSubscribeCheckCount);
    publishStat("$SYS/broker/load/aclchecks/subscribe/persecond", aclSubscribeChecksPerSecond);

    publishStat("$SYS/broker/load/aclchecks/registerwill/total", aclRegisterWillCheckCount);
    publishStat("$SYS/broker/load/aclchecks/registerwill/persecond", aclRegisterWillChecksPerSecond);

    std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;

    publishStat("$SYS/broker/retained messages/count", subscriptionStore->getRetainedMessageCount());

    publishStat("$SYS/broker/sessions/total", subscriptionStore->getSessionCount());

    publishStat("$SYS/broker/subscriptions/count", subscriptionStore->getSubscriptionCount());

    for (auto &pair : globals->stats.getExtras())
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

void ThreadData::publishBridgeState(std::shared_ptr<BridgeState> bridge, bool connected, const std::optional<std::string> &error)
{
    if (!bridge)
        return;

    {
        const std::string payload = connected ? "1" : "0";

        std::stringstream ss;
        ss << "$SYS/broker/bridge/" << bridge->c.clientidPrefix << "/connected";
        const std::string topic = ss.str();

        globals->stats.setExtra(topic, payload);

        Publish p(topic, payload, 0);
        publishWithAcl(p, true);
    }

    {
        const std::string message_on_no_error = connected ? "Connected" : "Not connected";
        const std::string message = error.value_or(message_on_no_error);
        const std::string topic = "$SYS/broker/bridge/" + bridge->c.clientidPrefix + "/connection_status";

        globals->stats.setExtra(topic, message);
        Publish p(topic, message, 0);
        publishWithAcl(p, true);
    }
}

void ThreadData::queueSettingRetainedMessage(const Publish &p, const std::vector<std::string> &subtopics, const std::chrono::time_point<std::chrono::steady_clock> limit)
{
    assert(pthread_self() == thread_id);
    const bool wakeup_required = this->queuedRetainedMessages.empty();
    this->queuedRetainedMessages.emplace_front(p, subtopics, limit);
    this->deferredRetainedMessagesSet.inc(1);

    if (wakeup_required)
        wakeUpThread();
}

void ThreadData::publishWithAcl(Publish &pub, bool setRetain)
{
    authentication.aclCheck(pub, pub.payload, AclAccess::write);

    PublishCopyFactory factory(&pub);
    std::shared_ptr<SubscriptionStore> subscriptionStore = globals->subscriptionStore;
    subscriptionStore->queuePacketAtSubscribers(factory, "", {}, true);

    if (setRetain)
        subscriptionStore->setRetainedMessage(pub, factory.getSubtopics());
}

/**
 * @brief ThreadData::sendQueuedWills is not an operation per thread, but it's good practice to perform certain tasks in the worker threads, where
 * the thread-local globals work.
 */
void ThreadData::sendQueuedWills()
{
    globals->subscriptionStore->sendQueuedWillMessages();
}

/**
 * @brief ThreadData::removeExpiredSessions is not an operation per thread, but it's good practice to perform certain tasks in the worker threads, where
 * the thread-local globals work.
 */
void ThreadData::removeExpiredSessions()
{
    globals->subscriptionStore->removeExpiredSessionsClients();
}

void ThreadData::purgeSubscriptionTree()
{
    bool done = globals->subscriptionStore->purgeSubscriptionTree();

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
    bool done = globals->subscriptionStore->expireRetainedMessages();

    if (!done)
    {
        auto f = std::bind(&ThreadData::removeExpiredRetainedMessages, this);

#ifdef TESTING
        addImmediateTask(f);
#else
        addDelayedTask(f, 100);
#endif
    }
}

void ThreadData::sendAllWills()
{
    auto clients_locked = clients.lock();

    for(auto &pair : clients_locked->by_fd)
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
        auto clients_locked = clients.lock();
        clientsFound.reserve(clients_locked->by_fd.size());

        for(auto &pair : clients_locked->by_fd)
        {
            clientsFound.push_back(pair.second);
        }
    }

    for (std::shared_ptr<Client> &c : clientsFound)
    {
        serverInitiatedDisconnect(c, ReasonCodes::ServerShuttingDown, "");
    }

    auto queued_collect_disconnecting_clients = [clientsFound, this](){
        for (const std::shared_ptr<Client> &c : clientsFound)
        {
            this->disconnectingClients.push_back(c);
        }

        uint64_t one = 1;
        check<std::runtime_error>(write(disconnectingAllEventFd, &one, sizeof(uint64_t)));
    };
    addImmediateTask(queued_collect_disconnecting_clients);
}

void ThreadData::removeQueuedClients()
{
    // Using shared pointers to have a claiming reference in case we lose the clients between the two locks.
    std::vector<std::shared_ptr<Client>> clients;

    {
        auto locked_clients_to_remove = clientsQueuedForRemoving.lock();

        for (const std::weak_ptr<Client> &c : *locked_clients_to_remove)
        {
            std::shared_ptr<Client> client = c.lock();
            if (client)
            {
                clients.push_back(client);
            }
        }

        locked_clients_to_remove->clear();
    }

    {
        auto clients_locked = this->clients.lock();
        for(const std::shared_ptr<Client> &client : clients)
        {
            const int fd = client->getFd();
            auto pos = clients_locked->by_fd.find(fd);
            if (pos != clients_locked->by_fd.end() && pos->second == client)
            {
                clients_locked->by_fd.erase(pos);
            }
        }
    }
}

void ThreadData::giveClient(std::shared_ptr<Client> &&client)
{
    const int fd = client->getFd();

    // A non-repeating keep-alive check is for when clients do a TCP connect and then nothing else.
    queueClientNextKeepAliveCheck(client, false);

    {
        auto clients_locked = clients.lock();
        clients_locked->by_fd[fd] = std::move(client); // We must give up ownership here, to avoid calling the client destructor in the main thread.
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd.get(), EPOLL_CTL_ADD, fd, &ev));
}

void ThreadData::giveBridge(std::shared_ptr<BridgeState> &bridgeState)
{
    if (!bridgeState)
        return;

    auto clients_locked = clients.lock();

    auto pos = clients_locked->bridges.find(bridgeState->c.clientidPrefix);

    if (pos != clients_locked->bridges.end())
    {
        std::shared_ptr<BridgeState> &existingState = pos->second;

        if (!existingState)
            existingState = bridgeState;
        else
        {
            if (existingState->c != bridgeState->c)
            {
                logger->log(LOG_NOTICE) << "Bridge '" << existingState->c.clientidPrefix << "' has changed. Reconnecting.";
                existingState = bridgeState;
            }
        }
    }
    else
    {
        clients_locked->bridges[bridgeState->c.clientidPrefix] = bridgeState;
    }
}

void ThreadData::removeBridgeQueued(const BridgeConfig &bridgeConfig, const std::string &reason)
{
    auto f = std::bind(&ThreadData::removeBridge, this, bridgeConfig, reason);
    auto task_queue_locked = taskQueue.lock();
    task_queue_locked->push_back(f);
    wakeUpThread();
}

void ThreadData::removeBridge(const BridgeConfig &bridgeConfig, const std::string &reason)
{
    auto clients_locked = clients.lock();

    auto pos = clients_locked->bridges.find(bridgeConfig.clientidPrefix);

    if (pos == clients_locked->bridges.end())
        return;

    std::shared_ptr<BridgeState> bridge = pos->second;
    clients_locked->bridges.erase(pos);

    if (!bridge)
        return;

    std::shared_ptr<Session> session = bridge->session.lock();

    if (!session)
        return;

    std::shared_ptr<Client> client = session->makeSharedClient();

    if (!client)
        return;

    if (!reason.empty())
        client->setDisconnectReason(reason);

    publishBridgeState(bridge, false, reason);
    removeClientQueued(client);
}

void ThreadData::setQueuedRetainedMessages()
{
    if (this->queuedRetainedMessages.empty())
        return;

    std::shared_ptr<SubscriptionStore> store = globals->subscriptionStore;

    if (!store)
        return;

    auto _pos = this->queuedRetainedMessages.begin();
    while (_pos != this->queuedRetainedMessages.end())
    {
        auto cur = _pos;
        _pos++;

        const bool try_lock_fail = cur->limit > std::chrono::steady_clock::now();

        if (!try_lock_fail)
        {
            deferredRetainedMessagesSetTimeout.inc(1);
        }

        if (store->setRetainedMessage(cur->p, cur->subtopics, try_lock_fail))
        {
            this->queuedRetainedMessages.erase(cur);
            continue;
        }
        else
        {
            wakeUpThread();
            return;
        }
    }
}

bool ThreadData::queuedRetainedMessagesEmpty() const
{
    return queuedRetainedMessages.empty();
}

void ThreadData::clearQueuedRetainedMessages()
{
    queuedRetainedMessages.clear();
}

void ThreadData::queueInternalHeartbeat()
{
    auto f = [this](std::chrono::time_point<std::chrono::steady_clock> t){
        this->driftCounter.update(t);

        if (this->driftCounter.getDrift() > settingsLocalCopy.maxEventLoopDrift)
            Logger::getInstance()->log(LOG_WARNING) << "Thread " << threadnr << " drift is: " << this->driftCounter.getDrift().count() << " ms";
    };

    {
        auto bound = std::bind(f, std::chrono::steady_clock::now());
        auto task_queue_locked = taskQueue.lock();
        task_queue_locked->push_back(bound);
    }

    wakeUpThread();
}

std::shared_ptr<Client> ThreadData::getClient(int fd)
{
    auto clients_locked = clients.lock();

    auto pos = clients_locked->by_fd.find(fd);

    if (pos == clients_locked->by_fd.end())
        return std::shared_ptr<Client>();

    return pos->second;
}

void ThreadData::removeClientQueued(const std::shared_ptr<Client> &client)
{
    // This is for same-thread calling, to avoid the calling thread to be slower and ending up with
    // the last reference on the shared pointer to client.
    assert(pthread_self() == thread_id);

    bool wakeUpNeeded = true;

    {
        auto locked_clients_to_remove = clientsQueuedForRemoving.lock();
        wakeUpNeeded = locked_clients_to_remove->empty();
        locked_clients_to_remove->push_front(client);
    }

    if (wakeUpNeeded)
    {
        auto f = std::bind(&ThreadData::removeQueuedClients, this);
        auto task_queue_locked = taskQueue.lock();
        task_queue_locked->push_back(f);

        wakeUpThread();
    }
}

void ThreadData::removeClientQueued(int fd)
{
    bool wakeUpNeeded = true;
    std::shared_ptr<Client> clientFound;

    {
        auto clients_locked = clients.lock();
        auto client_it = clients_locked->by_fd.find(fd);
        if (client_it != clients_locked->by_fd.end())
        {
            clientFound = client_it->second;
        }
    }

    if (clientFound)
    {
        {
            auto locked_clients_to_remove = clientsQueuedForRemoving.lock();
            wakeUpNeeded = locked_clients_to_remove->empty();
            locked_clients_to_remove->push_front(std::move(clientFound));
        }

        if (wakeUpNeeded)
        {
            auto f = std::bind(&ThreadData::removeQueuedClients, this);
            auto task_queue_locked = taskQueue.lock();
            task_queue_locked->push_back(f);

            wakeUpThread();
        }
    }
}

void ThreadData::removeClient(std::shared_ptr<Client> client)
{
    // This function is only for same-thread calling.
    assert(pthread_self() == thread_id);

    if (!client)
        return;

    client->setDisconnectStage(DisconnectStage::Now);

    auto clients_locked = clients.lock();
    auto pos = clients_locked->by_fd.find(client->getFd());
    if (pos != clients_locked->by_fd.end() && pos->second == client)
        clients_locked->by_fd.erase(pos);
}

void ThreadData::serverInitiatedDisconnect(std::shared_ptr<Client> &&client, ReasonCodes reason, const std::string &reason_text)
{
    auto c = std::move(client);
    serverInitiatedDisconnect(c, reason, reason_text);
}

/**
 * @brief ThreadData::serverInitiatedDisconnect queues a disconnect packet and when the last bytes are written, the thread loop will disconnect it.
 * @param client
 * @param reason
 * @param reason_text
 *
 * Sending clients disconnect packets is only supported by MQTT >= 5, so in case of MQTT3, just close the connection.
 *
 * There is a chance that an client's TCP buffers are full (when the client is gone, for example) and epoll will not report the
 * fd as EPOLLOUT, which means the disconnect will not happen. It will then be up to the keep-alive mechanism to kick the client out.
 */
void ThreadData::serverInitiatedDisconnect(const std::shared_ptr<Client> &client, ReasonCodes reason, const std::string &reason_text)
{
    if (!client)
        return;

    auto f = [client, reason, reason_text, this]() {
        if (!reason_text.empty())
            client->setDisconnectReason(reason_text);
        client->setDisconnectReason("Server initiating disconnect with reason: " + reasonCodeToString(reason));

        if (client->getProtocolVersion() >= ProtocolVersion::Mqtt5)
        {
            client->setDisconnectStage(DisconnectStage::SendPendingAppData);
            Disconnect d(ProtocolVersion::Mqtt5, reason);
            client->writeMqttPacket(d);
        }
        else
        {
            client->setDisconnectStage(DisconnectStage::Now);
            removeClientQueued(client);
        }
    };

    addImmediateTask(f);
}

void ThreadData::queueDoKeepAliveCheck()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::doKeepAliveCheck, this);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::queueQuit()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::quit, this);
    task_queue_locked->push_back(f);

    authentication.setQuitting();

    wakeUpThread();
}

void ThreadData::queuePasswdFileReload()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&Authentication::loadMosquittoPasswordFile, &authentication);
    task_queue_locked->push_back(f);

    auto f2 = std::bind(&Authentication::loadMosquittoAclFile, &authentication);
    task_queue_locked->push_back(f2);

    wakeUpThread();
}

int ThreadData::getNrOfClients()
{
    auto clients_locked = clients.lock();
    return clients_locked->by_fd.size();
}

void ThreadData::queuepluginPeriodicEvent()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::pluginPeriodicEvent, this);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::pluginPeriodicEvent()
{
    authentication.periodicEvent();
}

void ThreadData::queueSendWills()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::sendAllWills, this);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::queueSendDisconnects()
{
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::sendAllDisconnects, this);
    task_queue_locked->push_back(f);

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
    check<std::runtime_error>(epoll_ctl(this->epollfd.get(), mode, fd, &ev));
}

void ThreadData::pollExternalRemove(int fd)
{
    this->externalFds.erase(fd);
    if (epoll_ctl(this->epollfd.get(), EPOLL_CTL_DEL, fd, NULL) != 0)
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
        auto task_queue_locked = taskQueue.lock();
        wakeupNeeded = task_queue_locked->empty();
        task_queue_locked->push_back(f);
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

        int slotsTotal = 0;
        int slotsProcessed = 0;
        int clientsChecked = 0;

        {
            logger->logf(LOG_DEBUG, "Checking clients with pending keep-alive checks in thread %d", threadnr);

            auto queued_checks_locked = queuedKeepAliveChecks.lock();

            slotsTotal = queued_checks_locked->size();

            auto pos = queued_checks_locked->begin();
            while (pos != queued_checks_locked->end())
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

                        if (client->isOutgoingConnection() && client->getAuthenticated())
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

                pos = queued_checks_locked->erase(pos);
            }

            for (std::shared_ptr<Client> &c : clientsToRecheck)
            {
                c->resetBuffersIfEligible();
                queueClientNextKeepAliveCheck(c, true, queued_checks_locked);
            }
        }

        logger->logf(LOG_DEBUG, "Checked %d clients in %d of %d keep-alive slots in thread %d", clientsChecked, slotsProcessed, slotsTotal, threadnr);

        {
            auto clients_locked = clients.lock();

            for (std::shared_ptr<Client> c : clientsToRemove)
            {
                c->setDisconnectReason("Keep-alive expired: " + c->getKeepAliveInfoString());
                clients_locked->by_fd.erase(c->getFd());
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
    authentication.loadPlugin(*pluginLoader);
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

        {
            auto clients_locked = clients.lock();

            for (auto &pair : clients_locked->bridges)
            {
                std::shared_ptr<BridgeState> b = pair.second;

                if (!b)
                    continue;

                b->initSSL(true);
            }
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
    auto task_queue_locked = taskQueue.lock();

    auto f = std::bind(&ThreadData::reload, this, settings);
    task_queue_locked->push_back(f);

    wakeUpThread();
}

void ThreadData::wakeUpThread()
{
    uint64_t one = 1;
    check<std::runtime_error>(write(taskEventFd, &one, sizeof(uint64_t)));
}


ThreadDataOwner::ThreadDataOwner(int threadnr, const Settings &settings, const std::shared_ptr<PluginLoader> &pluginLoader) :
    td(std::make_shared<ThreadData>(threadnr, settings, pluginLoader))
{

}

ThreadDataOwner::~ThreadDataOwner()
{
    waitForQuit();
}

void ThreadDataOwner::start()
{
    this->thread = std::thread(&do_thread_work, td);

    pthread_t native = this->thread.native_handle();
    td->thread_id = native;
    std::ostringstream threadName;
    threadName << "FlashMQ T " << td->threadnr;
    threadName.flush();
    std::string name = threadName.str();
    const char *c_str = name.c_str();
    pthread_setname_np(native, c_str);
}

void ThreadDataOwner::waitForQuit()
{
    if (thread.joinable())
        thread.join();
}

ThreadData *ThreadDataOwner::operator->() const
{
    return td.get();
}

std::shared_ptr<ThreadData> ThreadDataOwner::getThreadData() const
{
    return td;
}
