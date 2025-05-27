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
#include "fdmanaged.h"
#include "mutexowned.h"

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

enum class DisconnectStage
{
    NotInitiated,
    SendPendingAppData, // Set this before making a client ready to write, and EPOLLOUT will take care of it.
    Now
};

struct AsyncAuthResult
{
    AuthResult result;
    std::string authMethod;
    std::string authData;

public:
    AsyncAuthResult(AuthResult result, const std::string authMethod, const std::string &authData);
};

class Client
{
    struct OutgoingTopicAliases
    {
        uint16_t cur_alias = 0;
        std::unordered_map<std::string, uint16_t> aliases;
    };

    struct WriteBuf
    {
        CirBuf buf;
        bool readyForWriting = false;

        WriteBuf(size_t size);
    };

    friend class IoWrapper;

    FdManaged fd;
    bool fuzzMode = false;

    ProtocolVersion protocolVersion = ProtocolVersion::None;

    uint32_t maxOutgoingPacketSize;
    const uint32_t maxIncomingPacketSize;

    uint16_t maxOutgoingTopicAliasValue = 0;
    uint16_t maxIncomingTopicAliasValue = 0;

    IoWrapper ioWrapper;
    std::string transportStr;

    CirBuf readbuf;
    MutexOwned<WriteBuf> writebuf;

    bool authenticated = false;
    bool connectPacketSeen = false;
    bool readyForReading = true;
    DisconnectStage disconnectStage = DisconnectStage::NotInitiated;
    bool outgoingConnection = false;
    bool outgoingConnectionEstablished = false;
    ClientType clientType = ClientType::Normal;
    bool supportsRetained = true; // Interestingly, only SERVERS can tell CLIENTS they don't support it (in CONNACK). The CONNECT packet has no field for it.
    std::string disconnectReason;
    std::chrono::time_point<std::chrono::steady_clock> lastActivity = std::chrono::steady_clock::now();

    std::string ssl_version;
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

    std::shared_ptr<Session> session;

    std::unordered_map<uint16_t, std::string> incomingTopicAliases;

    MutexOwned<OutgoingTopicAliases> outgoingTopicAliases;

    std::string extendedAuthenticationMethod;
    std::unique_ptr<ConnAck> stagedConnack;

    std::unique_ptr<StowedClientRegistrationData> registrationData;

    Logger *logger = Logger::getInstance();

    FMQSockaddr addr;

    std::weak_ptr<BridgeState> bridgeState;

    bool asyncAuthenticating = false;
    std::unique_ptr<std::vector<MqttPacket>> packetQueueAfterAsync;
    std::unique_ptr<AsyncAuthResult> asyncAuthResult;

    void setReadyForWriting(bool val);
    void setReadyForWriting(bool val, MutexLocked<WriteBuf> &writebuf);
    void setReadyForReading(bool val);
    void setAddr(const std::string &address);

public:
    uint8_t preAuthPacketCounter = 0;

    Client(int fd, std::shared_ptr<ThreadData> threadData, SSL *ssl, bool websocket, bool haproxy, const struct sockaddr *addr, const Settings &settings, bool fuzzMode=false);
    Client(const Client &other) = delete;
    Client(Client &&other) = delete;
    ~Client();

    int getFd() { return fd.get();}
    bool isSslAccepted() const;
    bool isSsl() const;
    bool needsHaProxyParsing() const;
    HaProxyConnectionType readHaProxyData();
    bool getSslReadWantsWrite() const;
    bool getSslWriteWantsRead() const;
    ProtocolVersion getProtocolVersion() const;
    void setProtocolVersion(ProtocolVersion version);
    void connectToBridgeTarget(FMQSockaddr addr);

    void startOrContinueSslHandshake();
    void setDisconnectStage(DisconnectStage val);
    DisconnectStage readFdIntoBuffer();
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
    const std::string &getClientId() { return this->clientid; }
    void setClientId(const std::string &id);
    const std::string &getUsername() const { return this->username; }
    std::string &getMutableUsername();
    std::shared_ptr<WillPublish> &getWill() { return this->willPublish; }
    const std::shared_ptr<WillPublish> &getStagedWill() { return this->stagedWillPublish; }
    void assignSession(const std::shared_ptr<Session> &session);
    std::shared_ptr<Session> getSession();
    void setDisconnectReason(const std::string &reason);
    std::chrono::seconds getSecondsTillKeepAliveAction() const;
    const std::optional<std::string> &getLocalPrefix() const;
    const std::optional<std::string> &getRemotePrefix() const;

    void writeText(const std::string &text);
    void writePing();
    void writePingResp();
    void writeLoginPacket();
    PacketDropReason writeMqttPacket(const MqttPacket &packet);
    PacketDropReason writeMqttPacketAndBlameThisClient(
        PublishCopyFactory &copyFactory, uint8_t max_qos, uint16_t packet_id, bool retain, uint32_t subscriptionIdentifier,
        const std::optional<std::string> &topic_override);
    PacketDropReason writeMqttPacketAndBlameThisClient(const MqttPacket &packet);
    void writeBufIntoFd();
    DisconnectStage getDisconnectStage() const { return disconnectStage; }

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

    void setAsyncAuthenticating() { this->asyncAuthenticating = true; }
    bool getAsyncAuthenticating() const { return this->asyncAuthenticating; }
    void addPacketToAfterAsyncQueue(MqttPacket &&p);
    void handleAfterAsyncQueue(std::shared_ptr<Client> &sender);
    void setAsyncAuthResult(const AsyncAuthResult &v);
    bool hasAsyncAuthResult() const { return this->asyncAuthResult.operator bool() ; }
    std::unique_ptr<AsyncAuthResult> stealAsyncAuthResult();
    bool getCleanStart() const { return clean_start;}

};

#endif // CLIENT_H
