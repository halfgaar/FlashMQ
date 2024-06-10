/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "client.h"

#include <cstring>
#include <sstream>
#include <iostream>
#include <cassert>
#include <chrono>
#include <netinet/tcp.h>

#include "logger.h"
#include "utils.h"
#include "threadglobals.h"
#include "subscriptionstore.h"
#include "mainapp.h"
#include "exceptions.h"

StowedClientRegistrationData::StowedClientRegistrationData(bool clean_start, uint16_t clientReceiveMax, uint32_t sessionExpiryInterval) :
    clean_start(clean_start),
    clientReceiveMax(clientReceiveMax),
    sessionExpiryInterval(sessionExpiryInterval)
{

}

/**
 * @brief Client::Client
 * @param fd
 * @param threadData
 * @param ssl
 * @param websocket
 * @param haproxy
 * @param addr
 * @param settings The client is constructed in the main thread, so we need to use its settings copy
 * @param fuzzMode
 */
Client::Client(int fd, std::shared_ptr<ThreadData> threadData, SSL *ssl, bool websocket, bool haproxy, struct sockaddr *addr, const Settings &settings, bool fuzzMode) :
    fd(fd),
    fuzzMode(fuzzMode),
    maxOutgoingPacketSize(settings.maxPacketSize),
    maxIncomingPacketSize(settings.maxPacketSize),
    maxIncomingTopicAliasValue(settings.maxIncomingTopicAliasValue), // Retaining snapshot of current setting, to not confuse clients when the setting changes.
    ioWrapper(ssl, websocket, settings.clientInitialBufferSize, this),
    readbuf(settings.clientInitialBufferSize),
    writebuf(settings.clientInitialBufferSize),
    epoll_fd(threadData ? threadData->epollfd : 0),
    threadData(threadData)
{
    ioWrapper.setHaProxy(haproxy);

    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    if (addr)
        memcpy(&this->addr, addr, sizeof(struct sockaddr_in6));
    else
        memset(&this->addr, 0, sizeof(struct sockaddr_in6));

    this->address = sockaddrToString(this->getAddr());

    const std::string haproxy_s = haproxy ? "/HAProxy" : "";
    const std::string ssl_s = ssl ? "/SSL" : "/Non-SSL";
    const std::string websocket_s = websocket ? "/Websocket" : "";

    transportStr = formatString("TCP%s%s%s", haproxy_s.c_str(), websocket_s.c_str(), ssl_s.c_str());

    // Avoid giving this log line for dummy clients.
    if (addr)
        logger->logf(LOG_NOTICE, "Accepting connection from: %s", repr_endpoint().c_str());
}

Client::~Client()
{
    // Dummy clients, that I sometimes need just because the interface demands it but there's not actually a client, have no thread.
    if (this->epoll_fd == 0)
        return;

    if (disconnectReason.empty())
        disconnectReason = "not specified";

    logger->logf(LOG_NOTICE, "Removing client '%s'. Reason(s): %s", repr().c_str(), disconnectReason.c_str());

    std::shared_ptr<ThreadData> td = this->threadData.lock();

    if (td)
        td->queueClientDisconnectActions(authenticated, this->getClientId(), std::move(willPublish), std::move(session), std::move(bridgeState));

    assert(!session);
    assert(!willPublish);

    if (fd > 0) // this check is essentially for testing, when working with a dummy fd.
    {
        if (epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, fd, NULL) != 0)
            logger->logf(LOG_ERR, "Removing fd %d of client '%s' from epoll produced error: %s", fd, repr().c_str(), strerror(errno));
        close(fd);
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

bool Client::needsHaProxyParsing() const
{
    return ioWrapper.needsHaProxyParsing();
}

HaProxyConnectionType Client::readHaProxyData()
{
    struct sockaddr* addr = reinterpret_cast<struct sockaddr*>(&this->addr);
    HaProxyConnectionType result = this->ioWrapper.readHaProxyData(this->fd, addr);
    this->address = sockaddrToString(this->getAddr());
    return result;
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

void Client::setProtocolVersion(ProtocolVersion version)
{
    this->protocolVersion = version;
}

void Client::connectToBridgeTarget(FMQSockaddr_in6 addr)
{
    this->lastActivity = std::chrono::steady_clock::now();

    std::shared_ptr<BridgeState> bridge = this->bridgeState.lock();

    if(!bridge)
        return;

    this->outgoingConnection = true;

    if (bridge->c.tcpNoDelay)
    {
        int tcp_nodelay_optval = 1;
        check<std::runtime_error>(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay_optval, sizeof(tcp_nodelay_optval)));
    }

    addr.setPort(bridge->c.port);
    int rc = connect(fd, addr.getSockaddr(), addr.getSize());

    if (rc < 0)
    {
        if (errno != EINPROGRESS)
           logger->logf(LOG_ERR, "Client connect error: %s", strerror(errno));
        return;
    }
    assert(rc == 0);

    setBridgeConnected();
}

void Client::startOrContinueSslHandshake()
{
    const bool acceptedBefore = isSslAccepted();

    ioWrapper.startOrContinueSslHandshake();

    if (this->outgoingConnection && !acceptedBefore && isSslAccepted())
        writeLoginPacket();
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
            const Settings *settings = ThreadGlobals::getSettings();
            // I guess I should have just made a 'max buffer size' option, and not distinguish between read/write?
            const uint32_t maxBufferSize = std::max<uint32_t>(this->maxIncomingPacketSize, settings->clientMaxWriteBufferSize);

            // We always grow for another iteration when there are still decoded websocket/SSL bytes, because epoll doesn't tell us that buffer has data.
            if (readbuf.getSize() * 2 <= maxBufferSize || error == IoWrapResult::WantRead || ioWrapper.hasProcessedBufferedBytesToRead())
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

void Client::writePing()
{
    std::lock_guard<std::mutex> locker(writeBufMutex);

    writebuf.ensureFreeSpace(2);

    writebuf.headPtr()[0] = 0b11000000;
    writebuf.advanceHead(1);
    writebuf.headPtr()[0] = 0;
    writebuf.advanceHead(1);

    setReadyForWriting(true);
}

PacketDropReason Client::writeMqttPacket(const MqttPacket &packet)
{
    const size_t packetSize = packet.getSizeIncludingNonPresentHeader();

    // "Where a Packet is too large to send, the Server MUST discard it without sending it and then behave as if it had completed
    // sending that Application Message [MQTT-3.1.2-25]."
    if (packetSize > this->maxOutgoingPacketSize)
    {
        return PacketDropReason::BiggerThanPacketLimit;
    }

    const Settings *settings = ThreadGlobals::getSettings();

    // After introducing the client_max_write_buffer_size with low default, this makes it somewhat backwards compatible with the default big packet size.
    const uint32_t growBufMaxTo = std::max<uint32_t>(settings->clientMaxWriteBufferSize, packetSize * 2);

    std::lock_guard<std::mutex> locker(writeBufMutex);

    // Grow as far as we can. We have to make room for one MQTT packet.
    writebuf.ensureFreeSpace(packetSize, growBufMaxTo);

    // And drop a publish when it doesn't fit, even after resizing. This means we do allow pings. And
    // QoS packet are queued and limited elsewhere.
    if (packet.packetType == PacketType::PUBLISH && packet.getQos() == 0 && packetSize > writebuf.freeSpace())
    {
        return PacketDropReason::BufferFull;
    }

    packet.readIntoBuf(writebuf);

    if (packet.packetType == PacketType::PUBLISH)
    {
        ThreadData *td = ThreadGlobals::getThreadData();
        td->sentMessageCounter.inc();
    }
    else if (packet.packetType == PacketType::DISCONNECT)
        setReadyForDisconnect();

    setReadyForWriting(true);

    return PacketDropReason::Success;
}

PacketDropReason Client::writeMqttPacketAndBlameThisClient(PublishCopyFactory &copyFactory, uint8_t max_qos, uint16_t packet_id, bool retain)
{
    uint16_t topic_alias = 0;
    uint16_t topic_alias_next = 0;
    bool skip_topic = false;

    /*
     * Required for two reasons:
     *
     * 1) Upon first use of an alias, we need to hold the lock until we know the packet is actually not dropped.
     * 2) Upon first use of an alias, we need to make sure another sender using the same topic won't get
     *    their packet sent first.
     *
     * I'm not fully happy that by doing this, we'll be holding two mutexes at the same time: this one and the buffer
     * write mutex, but it's OK for now. They are never locked in opposite order, so deadlocks shouldn't happen.
     */
    std::unique_lock<std::mutex> aliasMutexExtended;

    if (protocolVersion >= ProtocolVersion::Mqtt5 && this->maxOutgoingTopicAliasValue > 0)
    {
        std::unique_lock<std::mutex> aliasMutex(outgoingTopicAliasMutex);

        auto alias_pos = this->outgoingTopicAliases.find(copyFactory.getTopic());

        if (alias_pos != this->outgoingTopicAliases.end())
        {
            topic_alias = alias_pos->second;
            skip_topic = true;
        }
        else if (this->curOutgoingTopicAlias < this->maxOutgoingTopicAliasValue)
        {
            topic_alias_next = this->curOutgoingTopicAlias + 1;
            topic_alias = topic_alias_next;

            aliasMutexExtended = std::move(aliasMutex);
        }
    }

    MqttPacket *p = copyFactory.getOptimumPacket(max_qos, this->protocolVersion, topic_alias, skip_topic);

    assert(static_cast<bool>(p->getQos()) == static_cast<bool>(max_qos));

    if (p->getQos() > 0)
    {
        // This may change the packet ID and QoS of the incoming packet for each subscriber, but because we don't store that packet anywhere,
        // that should be fine.
        p->setPacketId(packet_id);
        p->setQos(copyFactory.getEffectiveQos(max_qos));
    }

    p->setRetain(retain);

    PacketDropReason dropReason = writeMqttPacketAndBlameThisClient(*p);

    if (dropReason == PacketDropReason::Success && topic_alias_next > 0)
    {
        this->outgoingTopicAliases[copyFactory.getTopic()] = topic_alias_next;
        this->curOutgoingTopicAlias = topic_alias_next;
    }

    return dropReason;
}

// Helper method to avoid the exception ending up at the sender of messages, which would then get disconnected.
PacketDropReason Client::writeMqttPacketAndBlameThisClient(const MqttPacket &packet)
{
    try
    {
        return this->writeMqttPacket(packet);
    }
    catch (std::exception &ex)
    {
        std::shared_ptr<ThreadData> td = this->threadData.lock();
        if (td)
            td->removeClientQueued(fd);

        return PacketDropReason::ClientError;
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

void Client::writeLoginPacket()
{
    std::shared_ptr<BridgeState> config = this->bridgeState.lock();

    if (!config)
        throw std::runtime_error("No bridge config in bridge?");

    Connect connectInfo(protocolVersion, clientid);
    connectInfo.username = config->c.remote_username;
    connectInfo.password = config->c.remote_password;
    connectInfo.clean_start = config->c.remoteCleanStart;
    connectInfo.keepalive = config->c.keepalive;
    connectInfo.bridgeProtocolBit = config->c.bridgeProtocolBit;

    if (config->c.remoteSessionExpiryInterval)
    {
        connectInfo.constructPropertyBuilder();
        connectInfo.propertyBuilder->writeSessionExpiry(config->c.remoteSessionExpiryInterval);
    }

    // We tell the other side they can send us topics with aliases, if set.
    if (this->maxIncomingTopicAliasValue)
    {
        connectInfo.constructPropertyBuilder();
        connectInfo.propertyBuilder->writeMaxTopicAliases(this->maxIncomingTopicAliasValue);
    }

    MqttPacket pack(connectInfo);
    writeMqttPacket(pack);
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

    setReadyForWriting(writebuf.usedBytes() > 0 || ioWrapper.hasPendingWrite() || error == IoWrapResult::Wouldblock);

    return true;
}

const sockaddr *Client::getAddr() const
{
    return reinterpret_cast<const struct sockaddr*>(&this->addr);
}

std::string Client::repr()
{
    std::string bridge;

    if (clientType == ClientType::Mqtt3DefactoBridge)
        bridge = "Mqtt3Bridge ";
    else if (clientType == ClientType::LocalBridge)
        bridge = "LocalBridge ";

    std::string s = formatString("[%sClientID='%s', username='%s', fd=%d, keepalive=%ds, transport='%s', address='%s', prot=%s, clean=%d]",
                                 bridge.c_str(), clientid.c_str(), username.c_str(), fd, keepalive, this->transportStr.c_str(), this->address.c_str(),
                                 protocolVersionString(protocolVersion).c_str(), this->clean_start);
    return s;
}

std::string Client::repr_endpoint()
{
    std::string s = formatString("address='%s', transport='%s', fd=%d",
                                 this->address.c_str(), this->transportStr.c_str(), fd);
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

    std::chrono::seconds x(keepalive + keepalive/2);
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
    const Settings *settings = ThreadGlobals::getSettings();
    const size_t initialBufferSize = settings->clientInitialBufferSize;

    readbuf.resetSizeIfEligable(initialBufferSize);
    ioWrapper.resetBuffersIfEligible();

    // Write buffers are written to from other threads, and this resetting takes place from the Client's own thread, so we need to lock.
    std::lock_guard<std::mutex> locker(writeBufMutex);
    writebuf.resetSizeIfEligable(initialBufferSize);
}

void Client::setTopicAlias(const uint16_t alias_id, const std::string &topic)
{
    if (alias_id == 0)
        throw ProtocolError("Client tried to set topic alias 0, which is a protocol error.", ReasonCodes::ProtocolError);

    if (topic.empty())
        return;

    // The specs actually say "The Client MUST NOT send a Topic Alias [...] to the Server greater than this value [Topic Alias Maximum]". So, it's not about count.
    if (alias_id > this->maxIncomingTopicAliasValue)
        throw ProtocolError(formatString("Client tried to set more topic aliases than the server max of %d per client", this->maxIncomingTopicAliasValue),
                            ReasonCodes::TopicAliasInvalid);

    this->incomingTopicAliases[alias_id] = topic;
}

const std::string &Client::getTopicAlias(const uint16_t id) const
{
    auto pos = this->incomingTopicAliases.find(id);

    if (pos == this->incomingTopicAliases.end())
        throw ProtocolError("Requesting topic alias ID (" + std::to_string(id) + ") that wasn't set before.", ReasonCodes::TopicAliasInvalid);

    return pos->second;
}

/**
 * @brief We use this for doing the checks on client traffic, as opposed to using settings.maxPacketSize, because the latter than change on config reload,
 *        possibly resulting in exceeding what the other side uses as maximum.
 * @return
 */
uint32_t Client::getMaxIncomingPacketSize() const
{
    return this->maxIncomingPacketSize;
}

/**
 * @brief We use this to send back in the connack, so we know we don't race with the value from settings, which may change during the connection handshake.
 * @return
 */
uint16_t Client::getMaxIncomingTopicAliasValue() const
{
    return this->maxIncomingTopicAliasValue;
}

void Client::sendOrQueueWill()
{
    if (this->threadData.expired())
        return;

    if (!this->willPublish)
        return;

    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    store->queueWillMessage(willPublish, clientid, session);
    this->willPublish.reset();
}

/**
 * @brief Client::setRegistrationData sets parameters for the session to be registered. We set them as arguments here to
 * possibly use later, because with extended authentication, session registration doesn't happen on the first CONNECT packet.
 * @param clean_start
 * @param maxQosPackets
 * @param sessionExpiryInterval
 */
void Client::setRegistrationData(bool clean_start, uint16_t client_receive_max, uint32_t sessionExpiryInterval)
{
    this->clean_start = clean_start;
    this->registrationData = std::make_unique<StowedClientRegistrationData>(clean_start, client_receive_max, sessionExpiryInterval);
}

const std::unique_ptr<StowedClientRegistrationData> &Client::getRegistrationData() const
{
    return this->registrationData;
}

void Client::clearRegistrationData()
{
    this->registrationData.reset();
}

/**
 * @brief Client::stageConnack saves the success connack for later use.
 * @param c
 *
 * The connack to be generated is known on the initial connect packet, but in extended authentication, the client won't get it
 * until the authentication is complete.
 */
void Client::stageConnack(std::unique_ptr<ConnAck> &&c)
{
    this->stagedConnack = std::move(c);
}

void Client::sendConnackSuccess()
{
    if (!stagedConnack)
    {
        throw ProtocolError("Programming bug: trying to send a prepared connack when there is none.", ReasonCodes::ProtocolError);
    }

    ConnAck &connAck = *this->stagedConnack.get();
    setAuthenticated(true);
    MqttPacket response(connAck);
    writeMqttPacket(response);
    logger->logf(LOG_NOTICE, "Client '%s' logged in successfully", repr().c_str());
    this->stagedConnack.reset();
}

void Client::sendConnackDeny(ReasonCodes reason)
{
    ConnAck connDeny(protocolVersion, reason, false);
    MqttPacket response(connDeny);
    setDisconnectReason("Access denied");
    setReadyForDisconnect();
    writeMqttPacket(response);
    logger->logf(LOG_NOTICE, "User '%s' access denied", username.c_str());
}

void Client::addAuthReturnDataToStagedConnAck(const std::string &authData)
{
    if (authData.empty())
        return;

    if (!stagedConnack)
    {
        throw ProtocolError("Programming bug: trying to add auth return data when there is no staged connack.", ReasonCodes::ProtocolError);
    }

    stagedConnack->propertyBuilder->writeAuthenticationData(authData);
}

void Client::setExtendedAuthenticationMethod(const std::string &authMethod)
{
    this->extendedAuthenticationMethod = authMethod;
}

const std::string &Client::getExtendedAuthenticationMethod() const
{
    return this->extendedAuthenticationMethod;
}

std::shared_ptr<ThreadData> Client::lockThreadData()
{
    return this->threadData.lock();
}

void Client::setBridgeState(std::shared_ptr<BridgeState> bridgeState)
{
    this->bridgeState = bridgeState;
    this->outgoingConnection = true;
    this->clientType = ClientType::LocalBridge;

    if (bridgeState)
    {
        this->protocolVersion = bridgeState->c.protocolVersion;
        this->address = bridgeState->c.address;
        this->clean_start = bridgeState->c.localCleanStart;
        this->clientid = bridgeState->c.getClientid();
        this->username = bridgeState->c.local_username.value_or(std::string());
        this->keepalive = bridgeState->c.keepalive;

        // Not setting maxOutgoingTopicAliasValue, because that must remain 0 until the other side says (in the connack) we can uses aliases.
        this->maxIncomingTopicAliasValue = bridgeState->c.maxIncomingTopicAliases;

        if (bridgeState->c.tlsMode > BridgeTLSMode::None)
        {
            const int mode = bridgeState->c.tlsMode == BridgeTLSMode::On ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
            ioWrapper.setSslVerify(mode, bridgeState->c.address);
        }
    }
}

bool Client::isOutgoingConnection() const
{
    return this->outgoingConnection;
}

std::shared_ptr<BridgeState> Client::getBridgeState()
{
    return this->bridgeState.lock();
}

void Client::setBridgeConnected()
{
    this->outgoingConnectionEstablished = true;

    std::shared_ptr<BridgeState> bridge = this->bridgeState.lock();

    if (bridge)
    {
        bridge->dnsResults.clear();
    }

    if (isSsl())
        this->startOrContinueSslHandshake();
    else
        this->writeLoginPacket();
}

bool Client::getOutgoingConnectionEstablished() const
{
    return this->outgoingConnectionEstablished;
}

void Client::setClientType(ClientType val)
{
    this->clientType = val;

    if (!session)
        return;

    session->setClientType(val);
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
    check<std::runtime_error>(epoll_ctl(this->epoll_fd, EPOLL_CTL_MOD, fd, &ev));
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
        check<std::runtime_error>(epoll_ctl(this->epoll_fd, EPOLL_CTL_MOD, fd, &ev));
    }
}

void Client::setAddr(const std::string &address)
{
    const Settings *settings = ThreadGlobals::getSettings();

    if (!settings->matchAddrWithSetRealIpFrom(&this->addr))
        return;

    bool success = false;

    {
        struct sockaddr_in *a = reinterpret_cast<struct sockaddr_in*>(&this->addr);
        success = inet_pton(AF_INET, address.c_str(), &a->sin_addr) > 0;
        if (success)
        {
            a->sin_port = 0;
            a->sin_family = AF_INET;
        }
    }

    if (!success)
    {
        success = inet_pton(AF_INET6, address.c_str(), &this->addr.sin6_addr) > 0;
        if (success)
        {
            this->addr.sin6_port = 0;
            this->addr.sin6_family = AF_INET6;
        }
    }

    if (success)
    {
        this->address = sockaddrToString(this->getAddr());
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
                                 uint32_t maxOutgoingPacketSize, uint16_t maxOutgoingTopicAliasValue)
{
    this->protocolVersion = protocolVersion;
    this->clientid = clientId;
    this->username = username;
    this->connectPacketSeen = connectPacketSeen;
    this->keepalive = keepalive;
    this->maxOutgoingPacketSize = maxOutgoingPacketSize;
    this->maxOutgoingTopicAliasValue = maxOutgoingTopicAliasValue;
}

void Client::setClientProperties(bool connectPacketSeen, uint16_t keepalive, uint32_t maxOutgoingPacketSize, uint16_t maxOutgoingTopicAliasValue, bool supportsRetained)
{
    logger->log(LOG_DEBUG) << "Client '" << repr() << "' properties set: keep_alive=" << keepalive << ", max_outgoing_packet_size=" << maxOutgoingPacketSize
                           << ", max_outgoing_topic_aliases=" << maxOutgoingTopicAliasValue << ".";

    this->connectPacketSeen = connectPacketSeen;
    this->keepalive = keepalive;
    this->maxOutgoingPacketSize = maxOutgoingPacketSize;
    this->maxOutgoingTopicAliasValue = maxOutgoingTopicAliasValue;
    this->supportsRetained = supportsRetained;
}

void Client::stageWill(WillPublish &&willPublish)
{
    this->stagedWillPublish = std::make_shared<WillPublish>(std::move(willPublish));
    this->stagedWillPublish->client_id = this->clientid;
    this->stagedWillPublish->username = this->username;
}

void Client::setWillFromStaged()
{
    this->willPublish = std::move(stagedWillPublish);
}

void Client::assignSession(const std::shared_ptr<Session> &session)
{
    this->session = session;
}

std::shared_ptr<Session> Client::getSession()
{
    return this->session;
}

void Client::resetSession()
{
    this->session.reset();
}

void Client::setDisconnectReason(const std::string &reason)
{
#ifndef TESTING // Because of testing trickery, we can't assert this in testing.
#ifndef NDEBUG
    auto td = this->threadData.lock();
    if (td)
    {
        assert(pthread_self() == td->thread.native_handle());
    }
#endif
#endif

    if (!this->disconnectReason.empty())
        this->disconnectReason += ", ";
    this->disconnectReason.append(reason);
}

/**
 * @brief Client::getSecondsTillKeepAliveAction gets the amount of seconds from now at which this client should be killed when
 * it was quiet, or in case of outgoing client, when a new ping is required.
 * @return
 *
 * "If the Keep Alive value is non-zero and the Server does not receive an MQTT Control Packet from the Client within one and a
 * half times the Keep Alive time period, it MUST close the Network Connection to the Client as if the network had failed [MQTT-3.1.2-22].
 */
std::chrono::seconds Client::getSecondsTillKeepAliveAction() const
{
    if (isOutgoingConnection())
        return std::chrono::seconds(this->keepalive);

    if (!this->authenticated)
        return std::chrono::seconds(30);

    if (this->keepalive == 0)
        return std::chrono::seconds(0);

    const uint32_t timeOfSilenceMeansKill = this->keepalive + (this->keepalive / 2) + 2;
    std::chrono::time_point<std::chrono::steady_clock> killTime = this->lastActivity + std::chrono::seconds(timeOfSilenceMeansKill);

    std::chrono::seconds secondsTillKillTime = std::chrono::duration_cast<std::chrono::seconds>(killTime - std::chrono::steady_clock::now());

    // We floor it, but also protect against the theoretically impossible negative value. Kill time shouldn't be in the past, because then we would
    // have killed it already.
    if (secondsTillKillTime < std::chrono::seconds(5))
        return std::chrono::seconds(5);

    return secondsTillKillTime;
}

void Client::clearWill()
{
    willPublish.reset();
    stagedWillPublish.reset();

    if (session)
        session->clearWill();
}

void Client::setClientId(const std::string &id)
{
    this->clientid = id;
}

std::string &Client::getMutableUsername()
{
    return this->username;
}

void Client::setSslVerify(X509ClientVerification verificationMode)
{
    const int mode = verificationMode > X509ClientVerification::None ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
    this->x509ClientVerification = verificationMode;
    ioWrapper.setSslVerify(mode, "");
}

std::optional<std::string> Client::getUsernameFromPeerCertificate()
{
    if (!ioWrapper.isSsl() || x509ClientVerification == X509ClientVerification::None)
        return std::optional<std::string>();

    X509Manager client_cert = ioWrapper.getPeerCertificate();

    if (!client_cert)
        throw ProtocolError("Client did not provide X509 peer certificate", ReasonCodes::BadUserNameOrPassword);

    X509_NAME *x509_name = X509_get_subject_name(client_cert.get());
    int index = X509_NAME_get_index_by_NID(x509_name, NID_commonName, -1);

    if (index < 0)
        return std::optional<std::string>();

    X509_NAME_ENTRY *name_entry = X509_NAME_get_entry(x509_name, index);

    if (!name_entry)
        throw std::runtime_error("X509_NAME_get_entry failed. This should be impossible.");

    ASN1_STRING *asn1_string = X509_NAME_ENTRY_get_data(name_entry);

    if (!asn1_string)
        throw std::runtime_error("Cannot obtain asn1 string from x509 certificate.");

    const unsigned char *str = ASN1_STRING_get0_data(asn1_string);

    if (!str)
        throw std::runtime_error("ASN1_STRING_get0_data failed. This should be impossible.");

    std::string username(reinterpret_cast<const char*>(str));

    if (!isValidUtf8(username))
        throw ProtocolError("Common name from peer certificate is not valid UTF8.", ReasonCodes::MalformedPacket);

    return username;
}

X509ClientVerification Client::getX509ClientVerification() const
{
    return x509ClientVerification;
}

void Client::setAllowAnonymousOverride(const AllowListenerAnonymous allow)
{
    allowAnonymousOverride = allow;
}

AllowListenerAnonymous Client::getAllowAnonymousOverride() const
{
    return allowAnonymousOverride;
}
