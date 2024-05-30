/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "threadloop.h"
#include "settings.h"
#include "threadglobals.h"
#include "mainapp.h"
#include "utils.h"
#include "exceptions.h"

void do_thread_work(ThreadData *threadData)
{
    maskAllSignalsCurrentThread();

    int epoll_fd = threadData->epollfd;
    ThreadGlobals::assign(&threadData->authentication);
    ThreadGlobals::assignThreadData(threadData);
    ThreadGlobals::assignSettings(&threadData->settingsLocalCopy);

    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    std::vector<MqttPacket> packetQueueIn;

    Logger *logger = Logger::getInstance();

    threadData->running = false;

    try
    {
        logger->logf(LOG_NOTICE, "Thread %d doing auth init.", threadData->threadnr);
        threadData->initplugin();
        threadData->running = true;
    }
    catch(std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error initializing auth back-end: %s", ex.what());
        MainApp *instance = MainApp::getMainApp();
        instance->quit();
    }

    while (threadData->running)
    {
        const uint32_t next_task_delay = threadData->delayedTasks.getTimeTillNext();
        const uint32_t epoll_wait_time = std::min<uint32_t>(next_task_delay, 100);

        int fdcount = epoll_wait(epoll_fd, events, MAX_EVENTS, epoll_wait_time);

        if (__builtin_expect(epoll_wait_time == 0, 0))
        {
            threadData->delayedTasks.performAll();
        }

        if (fdcount < 0)
        {
            if (errno == EINTR)
                continue;
            logger->logf(LOG_ERR, "Problem waiting for fd: %s", strerror(errno));
        }
        for (int i = 0; i < fdcount; i++)
        {
            struct epoll_event cur_ev = events[i];
            int fd = cur_ev.data.fd;

            if (fd == threadData->taskEventFd)
            {
                uint64_t eventfd_value = 0;
                check<std::runtime_error>(read(fd, &eventfd_value, sizeof(uint64_t)));

                std::list<std::function<void()>> copiedTasks;

                {
                    std::lock_guard<std::mutex> locker(threadData->taskQueueMutex);
                    copiedTasks = std::move(threadData->taskQueue);
                    threadData->taskQueue.clear();
                }

                for(auto &f : copiedTasks)
                {
                    try
                    {
                        f();
                    }
                    catch (std::exception &ex)
                    {
                        Logger *logger = Logger::getInstance();
                        logger->logf(LOG_ERR, "Error in queued task: %s", ex.what());
                    }
                }

                try
                {
                    threadData->setQueuedRetainedMessages();
                }
                catch (std::exception &ex)
                {
                    Logger *logger = Logger::getInstance();
                    logger->log(LOG_ERR) << "Error in setting queued retained messages: " << ex.what() << ". This shouldn't "
                                         << "happen and is likely a bug. Clearing the queue for safety.";
                    threadData->clearQueuedRetainedMessages();
                }

                continue;
            }

            std::shared_ptr<Client> client = threadData->getClient(fd);

            if (__builtin_expect(!client, 0))
            {
                // If the fd is not a client, it may be an externally monitored fd, from the plugin.
                auto pos = threadData->externalFds.find(fd);
                if (pos != threadData->externalFds.end())
                {
                    std::weak_ptr<void> &p = pos->second;
                    threadData->authentication.fdReady(fd, cur_ev.events, p);
                }

                continue;
            }

            try
            {
                if (__builtin_expect(client->needsHaProxyParsing(), 0))
                {
                    if (client->readHaProxyData() == HaProxyConnectionType::Local)
                    {
                        client->setDisconnectReason("HAProxy health check");
                    }
                }
                if (client->isOutgoingConnection() && !client->getOutgoingConnectionEstablished())
                {
                    int error = 0;
                    socklen_t optlen = sizeof(int);
                    int rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &optlen);

                    if (rc == 0 && error == 0)
                    {
                        client->setBridgeConnected();
                    }

                    if (error > 0 && error != EINPROGRESS)
                        throw std::runtime_error(strerror(error));

                    continue;
                }
                if (cur_ev.events & (EPOLLERR | EPOLLHUP))
                {
                    client->setDisconnectReason("epoll says socket is in ERR or HUP state.");
                    threadData->removeClient(client);
                    continue;
                }
                if (client->isSsl() && !client->isSslAccepted())
                {
                    client->startOrContinueSslHandshake();
                    continue;
                }
                if ((cur_ev.events & EPOLLIN) || ((cur_ev.events & EPOLLOUT) && client->getSslReadWantsWrite()))
                {
                    VectorClearGuard vectorClear(packetQueueIn);
                    bool readSuccess = client->readFdIntoBuffer();
                    client->bufferToMqttPackets(packetQueueIn, client);

                    for (MqttPacket &packet : packetQueueIn)
                    {
#ifdef TESTING
                        if (client->onPacketReceived)
                            client->onPacketReceived(packet);
                        else
#endif
                        packet.handle();
                    }

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
            catch (ProtocolError &ex)
            {
                client->setDisconnectReason(ex.what());
                bool clientRemoved = true;

                try
                {
                    if (!client->getAuthenticated())
                    {
                        ConnAck connAck(client->getProtocolVersion(), ex.reasonCode);

                        if (connAck.supported_reason_code)
                        {
                            MqttPacket p(connAck);
                            client->writeMqttPacket(p);
                            client->setReadyForDisconnect();
                        }
                        else
                        {
                            clientRemoved = false;
                        }
                    }
                    else if (client->getProtocolVersion() >= ProtocolVersion::Mqtt5)
                    {
                        Disconnect d(client->getProtocolVersion(), ex.reasonCode);
                        MqttPacket p(d);
                        client->writeMqttPacket(p);
                        client->setReadyForDisconnect();

                        // When a client's TCP buffers are full (when the client is gone, for instance), EPOLLOUT will never be
                        // reported. In those cases, the client is not removed; not until the keep-alive mechanism anyway. Is
                        // that a problem?
                    }
                    else
                    {
                        clientRemoved = false;
                    }
                }
                catch (std::exception &inner_ex)
                {
                    clientRemoved = false;
                    logger->log(LOG_ERR) << "Exception when notyfing client about ProtocolError: " << inner_ex.what();
                }

                if (!clientRemoved)
                {
                    logger->log(LOG_ERR) << "Unspecified or non-MQTT protocol error: " << ex.what() << ". Removing client.";
                    threadData->removeClient(client);
                }
            }
            catch(BadClientException &ex)
            {
                client->setDisconnectReason(ex.what());
                threadData->removeClient(client);
            }
            catch(std::exception &ex)
            {
                client->setDisconnectReason(ex.what());
                logger->log(LOG_ERR) << "Packet read/write error: " << ex.what() << ". Removing client " << client->repr();
                threadData->removeClient(client);
            }
        }
    }

    try
    {
        logger->logf(LOG_NOTICE, "Thread %d doing auth cleanup.", threadData->threadnr);
        threadData->cleanupplugin();
    }
    catch(std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error cleaning auth back-end: %s", ex.what());
    }

    threadData->finished = true;
}
