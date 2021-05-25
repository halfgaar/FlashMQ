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

#include "logger.h"

Client::Client(int fd, std::shared_ptr<ThreadData> threadData, SSL *ssl, bool websocket, std::shared_ptr<Settings> settings, bool fuzzMode) :
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
}

Client::~Client()
{
    // Will payload can be empty, apparently.
    if (!will_topic.empty())
    {
        std::shared_ptr<SubscriptionStore> &store = getThreadData()->getSubscriptionStore();

        Publish will(will_topic, will_payload, will_qos);
        will.retain = will_retain;
        const MqttPacket willPacket(will);

        const std::vector<std::string> subtopics = splitToVector(will_topic, '/');
        store->queuePacketAtSubscribers(subtopics, willPacket);
    }

    if (disconnectReason.empty())
        disconnectReason = "not specified";

    logger->logf(LOG_NOTICE, "Removing client '%s'. Reason(s): %s", repr().c_str(), disconnectReason.c_str());
    if (epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL) != 0)
        logger->logf(LOG_ERR, "Removing fd %d of client '%s' from epoll produced error: %s", fd, repr().c_str(), strerror(errno));
    close(fd);
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

    lastActivity = time(NULL);
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

void Client::writeMqttPacket(const MqttPacket &packet, const char qos)
{
    std::lock_guard<std::mutex> locker(writeBufMutex);

    // We have to allow big packets, yet don't allow a slow loris subscriber to grow huge write buffers. This
    // could be enhanced a lot, but it's a start.
    const uint32_t growBufMaxTo = std::min<int>(packet.getSizeIncludingNonPresentHeader() * 1000, maxPacketSize);

    // Grow as far as we can. We have to make room for one MQTT packet.
    writebuf.ensureFreeSpace(packet.getSizeIncludingNonPresentHeader(), growBufMaxTo);

    // And drop a publish when it doesn't fit, even after resizing. This means we do allow pings. And
    // QoS packet are queued and limited elsewhere.
    if (packet.packetType == PacketType::PUBLISH && qos == 0 && packet.getSizeIncludingNonPresentHeader() > writebuf.freeSpace())
    {
        return;
    }

    writebuf.ensureFreeSpace(packet.getSizeIncludingNonPresentHeader());

    if (!packet.containsFixedHeader())
    {
        writebuf.headPtr()[0] = packet.getFirstByte();
        writebuf.advanceHead(1);
        RemainingLength r = packet.getRemainingLength();
        writebuf.write(r.bytes, r.len);
    }

    writebuf.write(packet.getBites().data(), packet.getBites().size());

    if (packet.packetType == PacketType::DISCONNECT)
        setReadyForDisconnect();

    setReadyForWriting(true);
}

// Helper method to avoid the exception ending up at the sender of messages, which would then get disconnected.
void Client::writeMqttPacketAndBlameThisClient(const MqttPacket &packet, const char qos)
{
    try
    {
        this->writeMqttPacket(packet, qos);
    }
    catch (std::exception &ex)
    {
        threadData->removeClient(fd);
    }
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
    std::ostringstream a;
    a << "[Client=" << clientid << ", user=" << username << ", fd=" << fd << "]";
    a.flush();
    return a.str();
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

    if (!authenticated)
        return lastActivity + 20 < time(NULL);

    bool result = (lastActivity + (keepalive*10/5)) < time(NULL);
    return result;
}

std::string Client::getKeepAliveInfoString() const
{
    std::string s = "authenticated: " + std::to_string(authenticated) + ", keep-alive: " + std::to_string(keepalive) + "s, last activity "
            + std::to_string(time(NULL) - lastActivity) + " seconds ago.";
    return s;
}

void Client::resetBuffersIfEligible()
{
    readbuf.resetSizeIfEligable(initialBufferSize);
    writebuf.resetSizeIfEligable(initialBufferSize);
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

    if (disconnecting)
        return;

    if (ioWrapper.getSslReadWantsWrite())
        val = true;

    if (val == this->readyForWriting)
        return;

    readyForWriting = val;
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    if (readyForReading)
        ev.events |= EPOLLIN;
    if (readyForWriting)
        ev.events |= EPOLLOUT;
    check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_MOD, fd, &ev));
}

void Client::setReadyForReading(bool val)
{
#ifndef NDEBUG
    if (fuzzMode)
        return;
#endif

    if (disconnecting)
        return;

    if (val == this->readyForReading)
        return;

    readyForReading = val;
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    if (readyForReading)
        ev.events |= EPOLLIN;
    if (readyForWriting)
        ev.events |= EPOLLOUT;
    check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_MOD, fd, &ev));
}

bool Client::bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender)
{
    while (readbuf.usedBytes() >= MQTT_HEADER_LENGH)
    {
        // Determine the packet length by decoding the variable length
        int remaining_length_i = 1; // index of 'remaining length' field is one after start.
        uint fixed_header_length = 1;
        size_t multiplier = 1;
        size_t packet_length = 0;
        unsigned char encodedByte = 0;
        do
        {
            fixed_header_length++;

            if (fixed_header_length > 5)
                throw ProtocolError("Packet signifies more than 5 bytes in variable length header. Invalid.");

            // This happens when you only don't have all the bytes that specify the remaining length.
            if (fixed_header_length > readbuf.usedBytes())
                return false;

            encodedByte = readbuf.peakAhead(remaining_length_i++);
            packet_length += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            if (multiplier > 128*128*128*128)
                throw ProtocolError("Malformed Remaining Length.");
        }
        while ((encodedByte & 128) != 0);
        packet_length += fixed_header_length;

        if (!authenticated && packet_length >= 1024*1024)
        {
            throw ProtocolError("An unauthenticated client sends a packet of 1 MB or bigger? Probably it's just random bytes.");
        }

        if (packet_length > ABSOLUTE_MAX_PACKET_SIZE)
        {
            throw ProtocolError("A client sends a packet claiming to be bigger than the maximum MQTT allows.");
        }

        if (packet_length <= readbuf.usedBytes())
        {
            packetQueueIn.emplace_back(readbuf, packet_length, fixed_header_length, sender);
        }
        else
            break;
    }

    setReadyForReading(readbuf.freeSpace() > 0);

    return true;
}

void Client::setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive, bool cleanSession)
{
    this->protocolVersion = protocolVersion;
    this->clientid = clientId;
    this->username = username;
    this->connectPacketSeen = connectPacketSeen;
    this->keepalive = keepalive;
    this->cleanSession = cleanSession;
}

void Client::setWill(const std::string &topic, const std::string &payload, bool retain, char qos)
{
    this->will_topic = topic;
    this->will_payload = payload;
    this->will_retain = retain;
    this->will_qos = qos;
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
    will_topic.clear();
    will_payload.clear();
    will_retain = false;
    will_qos = 0;
}

