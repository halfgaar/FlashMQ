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

#include "client.h"

#include <cstring>
#include <sstream>
#include <iostream>
#include <cassert>
#include <chrono>

#include "logger.h"
#include "utils.h"
#include "threadglobals.h"

Client::Client(int fd, std::shared_ptr<ThreadData> threadData, SSL *ssl, bool websocket, struct sockaddr *addr, std::shared_ptr<Settings> settings, bool fuzzMode) :
    fd(fd),
    fuzzMode(fuzzMode),
    initialBufferSize(settings->clientInitialBufferSize), // The client is constructed in the main thread, so we need to use its settings copy
    maxPacketSize(settings->maxPacketSize), // Same as initialBufferSize comment.
    ioWrapper(ssl, websocket, initialBufferSize, this),
    readbuf(initialBufferSize),
    writebuf(initialBufferSize),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    this->address = sockaddrToString(addr);

    if (ssl)
        transportStr = websocket ? "TCP/Websocket/MQTT/SSL" : "TCP/MQTT/SSL";
    else
        transportStr = websocket ? "TCP/Websocket/MQTT/Non-SSL" : "TCP/MQTT/Non-SSL";
}

Client::~Client()
{
    std::shared_ptr<SubscriptionStore> &store = getThreadData()->getSubscriptionStore();

    // Will payload can be empty, apparently.
    if (willPublish)
    {
        store->queueWillMessage(willPublish);
    }

    if (disconnectReason.empty())
        disconnectReason = "not specified";

    logger->logf(LOG_NOTICE, "Removing client '%s'. Reason(s): %s", repr().c_str(), disconnectReason.c_str());
    if (fd > 0) // this check is essentially for testing, when working with a dummy fd.
    {
        if (epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL) != 0)
            logger->logf(LOG_ERR, "Removing fd %d of client '%s' from epoll produced error: %s", fd, repr().c_str(), strerror(errno));
        close(fd);
    }

    if (session->getDestroyOnDisconnect())
    {
        store->removeSession(session);
    }
    else
    {
        store->queueSessionRemoval(session);
    }
}

bool Client::isSslAccepted() const
{
    return ioWrapper.isSslAccepted();
}

bool Client::isSsl() const
{
    return ioWrapper.isSsl();
}

bool Client::getSslReadWantsWrite() const
{
    return ioWrapper.getSslReadWantsWrite();
}

bool Client::getSslWriteWantsRead() const
{
    return ioWrapper.getSslWriteWantsRead();
}

ProtocolVersion Client::getProtocolVersion() const
{
    return protocolVersion;
}

void Client::startOrContinueSslAccept()
{
    ioWrapper.startOrContinueSslAccept();
}

// Causes future activity on the client to cause a disconnect.
void Client::markAsDisconnecting()
{
    if (disconnecting)
        return;

    disconnecting = true;
}

// false means any kind of error we want to get rid of the client for.
bool Client::readFdIntoBuffer()
{
    if (disconnecting)
        return false;

    IoWrapResult error = IoWrapResult::Success;
    int n = 0;
    while (readbuf.freeSpace() > 0 && (n = ioWrapper.readWebsocketAndOrSsl(fd, readbuf.headPtr(), readbuf.maxWriteSize(), &error)) != 0)
    {
        if (n > 0)
        {
            readbuf.advanceHead(n);
        }

        if (error == IoWrapResult::Interrupted)
            continue;
        if (error == IoWrapResult::Wouldblock)
            break;

        // Make sure we either always have enough space for a next call of this method, or stop reading the fd.
        if (readbuf.freeSpace() == 0)
        {
            if (readbuf.getSize() * 2 < maxPacketSize)
            {
                readbuf.doubleSize();
            }
            else
            {
                setReadyForReading(false);
                break;
            }
        }
    }

    if (error == IoWrapResult::Disconnected)
    {
        return false;
    }

    lastActivity = std::chrono::steady_clock::now();
    if (session)
        session->touch(lastActivity);

    return true;
}

void Client::writeText(const std::string &text)
{
    assert(ioWrapper.isWebsocket());
    assert(ioWrapper.getWebsocketState() == WebsocketState::NotUpgraded);

    // Not necessary, because at this point, no other threads write to this client, but including for clarity.
    std::lock_guard<std::mutex> locker(writeBufMutex);

    writebuf.ensureFreeSpace(text.size());
    writebuf.write(text.c_str(), text.length());

    setReadyForWriting(true);
}

int Client::writeMqttPacket(const MqttPacket &packet)
{
    std::lock_guard<std::mutex> locker(writeBufMutex);

    // We have to allow big packets, yet don't allow a slow loris subscriber to grow huge write buffers. This
    // could be enhanced a lot, but it's a start.
    const uint32_t growBufMaxTo = std::min<int>(packet.getSizeIncludingNonPresentHeader() * 1000, maxPacketSize);

    // Grow as far as we can. We have to make room for one MQTT packet.
    writebuf.ensureFreeSpace(packet.getSizeIncludingNonPresentHeader(), growBufMaxTo);

    // And drop a publish when it doesn't fit, even after resizing. This means we do allow pings. And
    // QoS packet are queued and limited elsewhere.
    if (packet.packetType == PacketType::PUBLISH && packet.getQos() == 0 && packet.getSizeIncludingNonPresentHeader() > writebuf.freeSpace())
    {
        return 0;
    }

    packet.readIntoBuf(writebuf);

    if (packet.packetType == PacketType::DISCONNECT)
        setReadyForDisconnect();

    setReadyForWriting(true);
    return 1;
}

int Client::writeMqttPacketAndBlameThisClient(PublishCopyFactory &copyFactory, char max_qos, uint16_t packet_id)
{
    const Settings *settings = ThreadGlobals::getSettings();
    uint16_t topic_alias = 0;
    bool skip_topic = false;

    if (protocolVersion >= ProtocolVersion::Mqtt5 && settings->maxOutgoingTopicAliases > this->curOutgoingTopicAlias)
    {
        uint16_t &id = this->outgoingTopicAliases[copyFactory.getTopic()];

        if (id > 0)
            skip_topic = true;
        else
            id = ++this->curOutgoingTopicAlias;

        topic_alias = id;
    }

    MqttPacket *p = copyFactory.getOptimumPacket(max_qos, this->protocolVersion, topic_alias, skip_topic);

    assert(p->getQos() <= max_qos);

    if (p->getQos() > 0)
    {
        // This may change the packet ID and QoS of the incoming packet for each subscriber, but because we don't store that packet anywhere,
        // that should be fine.
        p->setPacketId(packet_id);
        p->setQos(max_qos);
    }

    return writeMqttPacketAndBlameThisClient(*p);
}

// Helper method to avoid the exception ending up at the sender of messages, which would then get disconnected.
int Client::writeMqttPacketAndBlameThisClient(const MqttPacket &packet)
{
    try
    {
        return this->writeMqttPacket(packet);
    }
    catch (std::exception &ex)
    {
        threadData->removeClientQueued(fd);
    }

    return 0;
}

// Ping responses are always the same, so hardcoding it for optimization.
void Client::writePingResp()
{
    std::lock_guard<std::mutex> locker(writeBufMutex);

    writebuf.ensureFreeSpace(2);

    writebuf.headPtr()[0] = 0b11010000;
    writebuf.advanceHead(1);
    writebuf.headPtr()[0] = 0;
    writebuf.advanceHead(1);

    setReadyForWriting(true);
}

bool Client::writeBufIntoFd()
{
    std::unique_lock<std::mutex> lock(writeBufMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return true;

    // We can abort the write; the client is about to be removed anyway.
    if (disconnecting)
        return false;

    IoWrapResult error = IoWrapResult::Success;
    int n;
    while (writebuf.usedBytes() > 0 || ioWrapper.hasPendingWrite())
    {
        n = ioWrapper.writeWebsocketAndOrSsl(fd, writebuf.tailPtr(), writebuf.maxReadSize(), &error);

        if (n > 0)
            writebuf.advanceTail(n);

        if (error == IoWrapResult::Interrupted)
            continue;
        if (error == IoWrapResult::Wouldblock)
            break;
    }

    const bool bufferHasData = writebuf.usedBytes() > 0;
    setReadyForWriting(bufferHasData || error == IoWrapResult::Wouldblock);

    return true;
}

std::string Client::repr()
{
    std::string s = formatString("[ClientID='%s', username='%s', fd=%d, keepalive=%ds, transport='%s', address='%s', prot=%s]",
                                 clientid.c_str(), username.c_str(), fd, keepalive, this->transportStr.c_str(), this->address.c_str(),
                                 protocolVersionString(protocolVersion).c_str());
    return s;
}

/**
 * @brief Client::keepAliveExpired
 * @return
 *
 * [MQTT-3.1.2-24]: "If the Keep Alive value is non-zero and the Server does not receive a Control Packet from the
 * Client within one and a half times the Keep Alive time period, it MUST disconnect the Network Connection to
 * the Client as if the network had failed."
 */
bool Client::keepAliveExpired()
{
    if (keepalive == 0)
        return false;

    const std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();

    if (!authenticated)
        return lastActivity + std::chrono::seconds(20) < now;

    std::chrono::seconds x(keepalive*10/5);
    bool result = (lastActivity + x) < now;
    return result;
}

std::string Client::getKeepAliveInfoString() const
{
    std::chrono::seconds secondsSinceLastActivity = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastActivity);

    std::string s = formatString("authenticated=%s, keep-alive=%ss, last activity=%s seconds ago.", std::to_string(authenticated).c_str(), std::to_string(keepalive).c_str(),
                                  std::to_string(secondsSinceLastActivity.count()).c_str());
    return s;
}

void Client::resetBuffersIfEligible()
{
    readbuf.resetSizeIfEligable(initialBufferSize);
    ioWrapper.resetBuffersIfEligible();

    // Write buffers are written to from other threads, and this resetting takes place from the Client's own thread, so we need to lock.
    std::lock_guard<std::mutex> locker(writeBufMutex);
    writebuf.resetSizeIfEligable(initialBufferSize);
}

void Client::setTopicAlias(const uint16_t alias_id, const std::string &topic)
{
    if (alias_id == 0)
        throw ProtocolError("Client tried to set topic alias 0, which is a protocol error.");

    if (topic.empty())
        return;

    if (alias_id > this->maxTopicAliases)
        throw ProtocolError("Client exceeded max topic aliases.");

    this->topicAliases[alias_id] = topic;
}

const std::string &Client::getTopicAlias(const uint16_t id)
{
    return this->topicAliases[id];
}

#ifndef NDEBUG
/**
 * @brief IoWrapper::setFakeUpgraded().
 */
void Client::setFakeUpgraded()
{
    ioWrapper.setFakeUpgraded();
}
#endif

// Call this from a place you know the writeBufMutex is locked, or we're still only doing SSL accept.
void Client::setReadyForWriting(bool val)
{
#ifndef NDEBUG
    if (fuzzMode)
        return;
#endif

#ifdef TESTING
    if (fd == 0)
        return;
#endif

    if (disconnecting)
        return;

    if (ioWrapper.getSslReadWantsWrite())
        val = true;

    // This looks a bit like a race condition, but all calls to this method should be under lock of writeBufMutex, so it should be OK.
    if (val == this->readyForWriting)
        return;

    readyForWriting = val;

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = readyForReading*EPOLLIN | readyForWriting*EPOLLOUT;
    check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_MOD, fd, &ev));
}

void Client::setReadyForReading(bool val)
{
#ifndef NDEBUG
    if (fuzzMode)
        return;
#endif

#ifdef TESTING
    if (fd == 0)
        return;
#endif

    if (disconnecting)
        return;

    // This looks a bit like a race condition, but all calls to this method are from a threads's event loop, so we should be OK.
    if (val == this->readyForReading)
        return;

    readyForReading = val;

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;

    {
        // Because setReadyForWriting is always called onder writeBufMutex, this prevents readiness race conditions.
        std::lock_guard<std::mutex> locker(writeBufMutex);

        ev.events = readyForReading*EPOLLIN | readyForWriting*EPOLLOUT;
        check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_MOD, fd, &ev));
    }
}

void Client::bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender)
{
    MqttPacket::bufferToMqttPackets(readbuf, packetQueueIn, sender);
    setReadyForReading(readbuf.freeSpace() > 0);
}

void Client::setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive)
{
    const Settings *settings = ThreadGlobals::getSettings();

    setClientProperties(protocolVersion, clientId, username, connectPacketSeen, keepalive, settings->maxPacketSize, 0);
}


void Client::setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive,
                                 uint32_t maxPacketSize, uint16_t maxTopicAliases)
{
    this->protocolVersion = protocolVersion;
    this->clientid = clientId;
    this->username = username;
    this->connectPacketSeen = connectPacketSeen;
    this->keepalive = keepalive;
    this->maxPacketSize = maxPacketSize;
    this->maxTopicAliases = maxTopicAliases;
}

void Client::setWill(Publish &&willPublish)
{
    this->willPublish = std::make_shared<Publish>(std::move(willPublish));
}

void Client::assignSession(std::shared_ptr<Session> &session)
{
    this->session = session;
}

std::shared_ptr<Session> Client::getSession()
{
    return this->session;
}

void Client::setDisconnectReason(const std::string &reason)
{
    if (!this->disconnectReason.empty())
        this->disconnectReason += ", ";
    this->disconnectReason.append(reason);
}

void Client::clearWill()
{
    willPublish.reset();
    session->clearWill();
}

