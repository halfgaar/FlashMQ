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

/**
 * @brief The StowedClient struct stores the client when doing an extended authentication, and we need to keep the info around how
 * the client will be registered once the authentication succeeds.
 */
struct StowedClientRegistrationData
{
    const bool clean_start;
    const uint16_t clientReceiveMax;
    const uint32_t sessionExpiryInterval;

    StowedClientRegistrationData(bool clean_start, uint16_t clientReceiveMax, uint32_t sessionExpiryInterval);
};

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
    const uint16_t maxIncomingTopicAliasValue;

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
    bool clean_start = false;

    std::shared_ptr<WillPublish> willPublish;

    const int epoll_fd;
    std::weak_ptr<ThreadData> threadData; // The thread (data) that this client 'lives' in.
    std::mutex writeBufMutex;

    std::shared_ptr<Session> session;

    std::unordered_map<uint16_t, std::string> incomingTopicAliases;

    uint16_t curOutgoingTopicAlias = 0;
    std::unordered_map<std::string, uint16_t> outgoingTopicAliases;

    std::string extendedAuthenticationMethod;
    std::unique_ptr<ConnAck> stagedConnack;

    std::unique_ptr<StowedClientRegistrationData> registrationData;

    Logger *logger = Logger::getInstance();

    void setReadyForWriting(bool val);
    void setReadyForReading(bool val);

public:
    Client(int fd, std::shared_ptr<ThreadData> threadData, SSL *ssl, bool websocket, struct sockaddr *addr, const Settings &settings, bool fuzzMode=false);
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
    void setWill(WillPublish &&willPublish);
    void clearWill();
    void setAuthenticated(bool value) { authenticated = value;}
    bool getAuthenticated() { return authenticated; }
    bool hasConnectPacketSeen() { return connectPacketSeen; }
    std::string &getClientId() { return this->clientid; }
    const std::string &getUsername() const { return this->username; }
    std::string &getMutableUsername();
    std::shared_ptr<WillPublish> &getWill() { return this->willPublish; }
    void assignSession(std::shared_ptr<Session> &session);
    std::shared_ptr<Session> getSession();
    void setDisconnectReason(const std::string &reason);
    std::chrono::seconds getSecondsTillKillTime() const;

    void writeText(const std::string &text);
    void writePingResp();
    void writeMqttPacket(const MqttPacket &packet);
    void writeMqttPacketAndBlameThisClient(PublishCopyFactory &copyFactory, char max_qos, uint16_t packet_id);
    void writeMqttPacketAndBlameThisClient(const MqttPacket &packet);
    bool writeBufIntoFd();
    bool isBeingDisconnected() const { return disconnectWhenBytesWritten; }
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
    uint16_t getMaxIncomingTopicAliasValue() const;

    void sendOrQueueWill();
    void serverInitiatedDisconnect(ReasonCodes reason);

    void setRegistrationData(bool clean_start, uint16_t client_receive_max, uint32_t sessionExpiryInterval);
    const std::unique_ptr<StowedClientRegistrationData> &getRegistrationData() const;
    void clearRegistrationData();

    void stageConnack(std::unique_ptr<ConnAck> &&c);
    void sendConnackSuccess();
    void sendConnackDeny(ReasonCodes reason);
    void addAuthReturnDataToStagedConnAck(const std::string &authData);

    void setExtendedAuthenticationMethod(const std::string &authMethod);
    const std::string &getExtendedAuthenticationMethod() const;

    std::shared_ptr<ThreadData> lockThreadData();

#ifdef TESTING
    std::function<void(MqttPacket &packet)> onPacketReceived;
#endif

#ifndef NDEBUG
    void setFakeUpgraded();
#endif

};

#endif // CLIENT_H
