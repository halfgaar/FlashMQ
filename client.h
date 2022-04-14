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

#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <iostream>
#include <time.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "forward_declarations.h"

#include "threaddata.h"
#include "mqttpacket.h"
#include "exceptions.h"
#include "cirbuf.h"
#include "types.h"
#include "iowrapper.h"

#include "publishcopyfactory.h"

#define MQTT_HEADER_LENGH 2


class Client
{
    friend class IoWrapper;

    int fd;
    bool fuzzMode = false;

    ProtocolVersion protocolVersion = ProtocolVersion::None;

    const size_t initialBufferSize = 0;
    uint32_t maxOutgoingPacketSize;
    const uint32_t maxIncomingPacketSize;

    uint16_t maxOutgoingTopicAliasValue = 0;

    IoWrapper ioWrapper;
    std::string transportStr;
    std::string address;

    CirBuf readbuf;
    CirBuf writebuf;

    bool authenticated = false;
    bool connectPacketSeen = false;
    bool readyForWriting = false;
    bool readyForReading = true;
    bool disconnectWhenBytesWritten = false;
    bool disconnecting = false;
    std::string disconnectReason;
    std::chrono::time_point<std::chrono::steady_clock> lastActivity;

    std::string clientid;
    std::string username;
    uint16_t keepalive = 0;

    std::shared_ptr<Publish> willPublish;

    std::shared_ptr<ThreadData> threadData;
    std::mutex writeBufMutex;

    std::shared_ptr<Session> session;

    std::unordered_map<uint16_t, std::string> incomingTopicAliases;

    uint16_t curOutgoingTopicAlias = 0;
    std::unordered_map<std::string, uint16_t> outgoingTopicAliases;

    Logger *logger = Logger::getInstance();

    void setReadyForWriting(bool val);
    void setReadyForReading(bool val);

public:
    Client(int fd, std::shared_ptr<ThreadData> threadData, SSL *ssl, bool websocket, struct sockaddr *addr, const Settings *settings, bool fuzzMode=false);
    Client(const Client &other) = delete;
    Client(Client &&other) = delete;
    ~Client();

    int getFd() { return fd;}
    bool isSslAccepted() const;
    bool isSsl() const;
    bool getSslReadWantsWrite() const;
    bool getSslWriteWantsRead() const;
    ProtocolVersion getProtocolVersion() const;

    void startOrContinueSslAccept();
    void markAsDisconnecting();
    bool readFdIntoBuffer();
    void bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender);
    void setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive);
    void setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive,
                             uint32_t maxOutgoingPacketSize, uint16_t maxOutgoingTopicAliasValue);
    void setWill(const std::string &topic, const std::string &payload, bool retain, char qos);
    void setWill(Publish &&willPublish);
    void clearWill();
    void setAuthenticated(bool value) { authenticated = value;}
    bool getAuthenticated() { return authenticated; }
    bool hasConnectPacketSeen() { return connectPacketSeen; }
    std::shared_ptr<ThreadData> getThreadData() { return threadData; }
    std::string &getClientId() { return this->clientid; }
    const std::string &getUsername() const { return this->username; }
    std::shared_ptr<Publish> &getWill() { return this->willPublish; }
    void assignSession(std::shared_ptr<Session> &session);
    std::shared_ptr<Session> getSession();
    void setDisconnectReason(const std::string &reason);

    void writeText(const std::string &text);
    void writePingResp();
    int writeMqttPacket(const MqttPacket &packet);
    int writeMqttPacketAndBlameThisClient(PublishCopyFactory &copyFactory, char max_qos, uint16_t packet_id);
    int writeMqttPacketAndBlameThisClient(const MqttPacket &packet);
    bool writeBufIntoFd();
    bool readyForDisconnecting() const { return disconnectWhenBytesWritten && writebuf.usedBytes() == 0; }

    // Do this before calling an action that makes this client ready for writing, so that the EPOLLOUT will handle it.
    void setReadyForDisconnect() { disconnectWhenBytesWritten = true; }

    std::string repr();
    bool keepAliveExpired();
    std::string getKeepAliveInfoString() const;
    void resetBuffersIfEligible();

    void setTopicAlias(const uint16_t alias_id, const std::string &topic);
    const std::string &getTopicAlias(const uint16_t id);

    uint32_t getMaxIncomingPacketSize() const;

    void sendOrQueueWill();

#ifndef NDEBUG
    void setFakeUpgraded();
#endif

};

#endif // CLIENT_H
