/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <iostream>
#include <time.h>
#include <optional>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "forward_declarations.h"

#include "mqttpacket.h"
#include "cirbuf.h"
#include "types.h"
#include "iowrapper.h"
#include "bridgeconfig.h"
#include "enums.h"

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

    uint32_t maxOutgoingPacketSize;
    const uint32_t maxIncomingPacketSize;

    uint16_t maxOutgoingTopicAliasValue = 0;
    uint16_t maxIncomingTopicAliasValue = 0;

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
    bool outgoingConnection = false;
    bool outgoingConnectionEstablished = false;
    ClientType clientType = ClientType::Normal;
    bool supportsRetained = true; // Interestingly, only SERVERS can tell CLIENTS they don't support it (in CONNACK). The CONNECT packet has no field for it.
    std::string disconnectReason;
    std::chrono::time_point<std::chrono::steady_clock> lastActivity = std::chrono::steady_clock::now();

    std::string clientid;
    std::string username;
    uint16_t keepalive = 10;
    bool clean_start = false;
    X509ClientVerification x509ClientVerification = X509ClientVerification::None;
    AllowListenerAnonymous allowAnonymousOverride = AllowListenerAnonymous::None;

    std::shared_ptr<WillPublish> stagedWillPublish;
    std::shared_ptr<WillPublish> willPublish;

    const int epoll_fd;
    std::weak_ptr<ThreadData> threadData; // The thread (data) that this client 'lives' in.
    std::mutex writeBufMutex;

    std::shared_ptr<Session> session;

    std::unordered_map<uint16_t, std::string> incomingTopicAliases;

    uint16_t curOutgoingTopicAlias = 0;
    std::unordered_map<std::string, uint16_t> outgoingTopicAliases;
    std::mutex outgoingTopicAliasMutex;

    std::string extendedAuthenticationMethod;
    std::unique_ptr<ConnAck> stagedConnack;

    std::unique_ptr<StowedClientRegistrationData> registrationData;

    Logger *logger = Logger::getInstance();

    sockaddr_in6 addr;

    std::weak_ptr<BridgeState> bridgeState;

    void setReadyForWriting(bool val);
    void setReadyForReading(bool val);
    void setAddr(const std::string &address);

public:
    uint8_t preAuthPacketCounter = 0;

    Client(int fd, std::shared_ptr<ThreadData> threadData, SSL *ssl, bool websocket, bool haproxy, struct sockaddr *addr, const Settings &settings, bool fuzzMode=false);
    Client(const Client &other) = delete;
    Client(Client &&other) = delete;
    ~Client();

    int getFd() { return fd;}
    bool isSslAccepted() const;
    bool isSsl() const;
    bool needsHaProxyParsing() const;
    HaProxyConnectionType readHaProxyData();
    bool getSslReadWantsWrite() const;
    bool getSslWriteWantsRead() const;
    ProtocolVersion getProtocolVersion() const;
    void setProtocolVersion(ProtocolVersion version);
    void connectToBridgeTarget(FMQSockaddr_in6 addr);

    void startOrContinueSslHandshake();
    void markAsDisconnecting();
    bool readFdIntoBuffer();
    void bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender);
    void setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive);
    void setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive,
                             uint32_t maxOutgoingPacketSize, uint16_t maxOutgoingTopicAliasValue);
    void setClientProperties(bool connectPacketSeen, uint16_t keepalive, uint32_t maxOutgoingPacketSize, uint16_t maxOutgoingTopicAliasValue, bool supportsRetained);
    void setWill(const std::string &topic, const std::string &payload, bool retain, uint8_t qos);
    void stageWill(WillPublish &&willPublish);
    void setWillFromStaged();
    void clearWill();
    void setAuthenticated(bool value) { authenticated = value;}
    bool getAuthenticated() { return authenticated; }
    bool hasConnectPacketSeen() { return connectPacketSeen; }
    void setHasConnectPacketSeen() { connectPacketSeen = true; }
    std::string &getClientId() { return this->clientid; }
    void setClientId(const std::string &id);
    const std::string &getUsername() const { return this->username; }
    std::string &getMutableUsername();
    std::shared_ptr<WillPublish> &getWill() { return this->willPublish; }
    const std::shared_ptr<WillPublish> &getStagedWill() { return this->stagedWillPublish; }
    void assignSession(std::shared_ptr<Session> &session);
    std::shared_ptr<Session> getSession();
    void resetSession();
    void setDisconnectReason(const std::string &reason);
    std::chrono::seconds getSecondsTillKeepAliveAction() const;

    void writeText(const std::string &text);
    void writePing();
    void writePingResp();
    void writeLoginPacket();
    PacketDropReason writeMqttPacket(const MqttPacket &packet);
    PacketDropReason writeMqttPacketAndBlameThisClient(PublishCopyFactory &copyFactory, uint8_t max_qos, uint16_t packet_id, bool retain);
    PacketDropReason writeMqttPacketAndBlameThisClient(const MqttPacket &packet);
    bool writeBufIntoFd();
    bool isBeingDisconnected() const { return disconnectWhenBytesWritten; }
    bool readyForDisconnecting() const { return disconnectWhenBytesWritten && writebuf.usedBytes() == 0; }

    // Do this before calling an action that makes this client ready for writing, so that the EPOLLOUT will handle it.
    void setReadyForDisconnect() { disconnectWhenBytesWritten = true; }

    const sockaddr *getAddr() const;
    std::string repr();
    std::string repr_endpoint();
    bool keepAliveExpired();
    std::string getKeepAliveInfoString() const;
    void resetBuffersIfEligible();

    void setTopicAlias(const uint16_t alias_id, const std::string &topic);
    const std::string &getTopicAlias(const uint16_t id) const;

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

    void setBridgeState(std::shared_ptr<BridgeState> bridgeState);
    bool isOutgoingConnection() const;
    std::shared_ptr<BridgeState> getBridgeState();
    void setBridgeConnected();
    bool getOutgoingConnectionEstablished() const;
    ClientType getClientType() const { return clientType; }
    void setClientType(ClientType val);
    bool isRetainedAvailable() const {return supportsRetained; };

#ifdef TESTING
    std::function<void(MqttPacket &packet)> onPacketReceived;
#endif

#ifndef NDEBUG
    void setFakeUpgraded();
#endif

    void setSslVerify(X509ClientVerification verificationMode);
    std::optional<std::string> getUsernameFromPeerCertificate();
    X509ClientVerification getX509ClientVerification() const;
    void setAllowAnonymousOverride(const AllowListenerAnonymous allow);
    AllowListenerAnonymous getAllowAnonymousOverride() const;

};

#endif // CLIENT_H
