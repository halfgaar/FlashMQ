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

#include "threadloop.h"

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

                    std::list<std::function<void()>> copiedTasks;

                    {
                        std::lock_guard<std::mutex> locker(threadData->taskQueueMutex);
                        copiedTasks = std::move(threadData->taskQueue);
                        threadData->taskQueue.clear();
                    }

                    for(auto &f : copiedTasks)
                    {
                        f();
                    }

                    continue;
                }

                std::shared_ptr<Client> client = threadData->getClient(fd);

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
                        if (client->getProtocolVersion() >= ProtocolVersion::Mqtt5 && client->hasConnectPacketSeen())
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
                            logger->logf(LOG_ERR, "Protocol error: %s. Removing client.", ex.what());
                            threadData->removeClient(client);
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
