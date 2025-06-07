/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <list>
#include <string>
#include <memory>
#include <chrono>
#include <vector>
#include <optional>

#include "forward_declarations.h"
#include "nocopy.h"

#define FMQ_CLIENT_GROUP_ID "fmq_client_group_id"

enum class PacketType
{
    Reserved = 0,
    CONNECT = 1,
    CONNACK = 2,
    PUBLISH = 3,
    PUBACK = 4,
    PUBREC = 5,
    PUBREL = 6,
    PUBCOMP = 7,
    SUBSCRIBE = 8,
    SUBACK = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK = 11,
    PINGREQ = 12,
    PINGRESP = 13,
    DISCONNECT = 14,
    AUTH = 15
};

enum class ProtocolVersion
{
    None = 0,
    Mqtt31 = 0x03,
    Mqtt311 = 0x04,
    Mqtt5 = 0x05
};

enum class Mqtt5Properties
{
    None = 0,
    PayloadFormatIndicator = 1,
    MessageExpiryInterval = 2,
    ContentType = 3,
    ResponseTopic = 8,
    CorrelationData = 9,
    SubscriptionIdentifier = 11,
    SessionExpiryInterval = 17,
    AssignedClientIdentifier = 18,
    ServerKeepAlive = 19,
    AuthenticationMethod = 21,
    AuthenticationData = 22,
    RequestProblemInformation = 23,
    WillDelayInterval = 24,
    RequestResponseInformation = 25,
    ResponseInformation = 26,
    ServerReference = 28,
    ReasonString = 31,
    ReceiveMaximum = 33,
    TopicAliasMaximum = 34,
    TopicAlias = 35,
    MaximumQoS = 36,
    RetainAvailable = 37,
    UserProperty = 38,
    MaximumPacketSize = 39,
    WildcardSubscriptionAvailable = 40,
    SubscriptionIdentifierAvailable = 41,
    SharedSubscriptionAvailable = 42
};

/**
 * @brief The ConnAckReturnCodes enum are for MQTT3
 */
enum class ConnAckReturnCodes
{
    Accepted = 0,
    UnacceptableProtocolVersion = 1,
    ClientIdRejected = 2,
    ServerUnavailable = 3,
    MalformedUsernameOrPassword = 4,
    NotAuthorized = 5
};

/**
 * @brief The ReasonCodes enum are for MQTT5.
 */
enum class ReasonCodes
{
    Success = 0,
    GrantedQoS0 = 0,
    GrantedQoS1 = 1,
    GrantedQoS2 = 2,
    DisconnectWithWill = 4,
    NoMatchingSubscribers = 16,
    NoSubscriptionExisted = 17,
    ContinueAuthentication = 24,
    ReAuthenticate = 25,
    UnspecifiedError = 128,
    MalformedPacket = 129,
    ProtocolError = 130,
    ImplementationSpecificError = 131,
    UnsupportedProtocolVersion = 132,
    ClientIdentifierNotValid = 133,
    BadUserNameOrPassword = 134,
    NotAuthorized = 135,
    ServerUnavailable = 136,
    ServerBusy = 137,
    Banned = 138,
    ServerShuttingDown = 139,
    BadAuthenticationMethod = 140,
    KeepAliveTimeout = 141,
    SessionTakenOver = 142,
    TopicFilterInvalid = 143,
    TopicNameInvalid = 144,
    PacketIdentifierInUse = 145,
    PacketIdentifierNotFound = 146,
    ReceiveMaximumExceeded = 147,
    TopicAliasInvalid = 148,
    PacketTooLarge = 149,
    MessageRateTooHigh = 150,
    QuotaExceeded = 151,
    AdministrativeAction = 152,
    PayloadFormatInvalid = 153,
    RetainNotSupported = 154,
    QosNotSupported = 155,
    UseAnotherServer = 156,
    ServerMoved = 157,
    SharedSubscriptionsNotSupported = 158,
    ConnectionRateExceeded = 159,
    MaximumConnectTime = 160,
    SubscriptionIdentifiersNotSupported = 161,
    WildcardSubscriptionsNotSupported = 162
};

/**
 * What is primarily important to know, is that in MQTT3 there is a defacto standard that the MSB in the
 * protocol version byte signifies the client is a bridge. This helps in loop prevention. MQTT5 no longer
 * does that, because it has 'subscription options' for that.
 */
enum class ClientType : uint8_t
{
    Normal,
    Mqtt3DefactoBridge,
    LocalBridge
};

enum class RetainHandling: uint8_t
{
    SendRetainedMessagesAtSubscribe = 0,
    SendRetainedMessagesAtNewSubscribeOnly = 1,
    DoNotSendRetainedMessages = 2
};

struct SubscriptionOptionsByte
{
    const uint8_t b = 0;

    SubscriptionOptionsByte(uint8_t byte);
    SubscriptionOptionsByte(uint8_t qos, bool noLocal, bool retainAsPublished, RetainHandling retainHandling);

    bool getNoLocal() const;
    bool getRetainAsPublished() const;
    RetainHandling getRetainHandling() const;
    uint8_t getQos() const;
};

class ConnAck
{
public:
    ConnAck(const ProtocolVersion protVersion, ReasonCodes return_code, bool session_present=false);

    const ProtocolVersion protocol_version;
    uint8_t return_code;
    bool session_present = false;
    bool supported_reason_code = false;
    std::shared_ptr<Mqtt5PropertyBuilder> propertyBuilder;

    size_t getLengthWithoutFixedHeader() const;
};

class SubAck
{
public:
    const ProtocolVersion protocol_version;
    const uint16_t packet_id;
    std::list<ReasonCodes> responses;
    std::shared_ptr<Mqtt5PropertyBuilder> propertyBuilder;

    SubAck(const ProtocolVersion protVersion, uint16_t packet_id, const std::list<ReasonCodes> &subs_qos_reponses);
    size_t getLengthWithoutFixedHeader() const;
};

class UnsubAck
{
public:
    const ProtocolVersion protocol_version;
    const uint16_t packet_id;
    std::shared_ptr<Mqtt5PropertyBuilder> propertyBuilder;
    std::vector<ReasonCodes> reasonCodes;
    UnsubAck(const ProtocolVersion protVersion, uint16_t packet_id, const int unsubCount);
    size_t getLengthWithoutFixedHeader() const;
};

struct PublishExpireInfo
{
    std::chrono::time_point<std::chrono::steady_clock> createdAt = std::chrono::steady_clock::now();
    std::chrono::seconds expiresAfter;

    std::chrono::time_point<std::chrono::steady_clock> expiresAt() const;
    std::chrono::seconds getCurrentTimeToExpire() const;
};

class Publish
{
public:
    std::string client_id;
    std::string username;
    std::string topic;
    std::string payload;
private:
    NoCopy<std::vector<std::string>> subtopics;
public:

    uint8_t qos = 0;
    bool retain = false; // Note: existing subscribers don't get publishes of retained messages with retain=1. [MQTT-3.3.1-9]
    uint16_t topicAlias = 0;
    bool skipTopic = false;
    bool payloadUtf8 = false;
    std::optional<PublishExpireInfo> expireInfo;
    std::optional<std::string> correlationData;
    std::optional<std::string> responseTopic;
    std::optional<std::string> contentType;
    uint32_t subscriptionIdentifier = 0;
#ifdef TESTING
    uint32_t subscriptionIdentifierTesting = 0; // Clunky...
#endif
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> userProperties;

    Publish() = default;
    Publish(const std::string &topic, const std::string &payload, uint8_t qos);
    bool hasExpired() const;

    template<typename T>
    T getAge() const
    {
        if (!expireInfo)
            return T(0);

        return std::chrono::duration_cast<T>(std::chrono::steady_clock::now() - this->expireInfo->createdAt);
    }

    std::chrono::seconds getAge() const;
    std::vector<std::pair<std::string, std::string>> *getUserProperties() const;
    void addUserProperty(const std::string &key, const std::string &val);
    void addUserProperty(std::string &&key, std::string &&val);
    std::optional<std::string> getFirstUserProperty(const std::string &key) const;
    std::optional<Mqtt5PropertyBuilder> getPropertyBuilder() const;

    void setExpireAfter(uint32_t s);
    void setExpireAfterToCeiling(std::chrono::seconds s);

    const std::vector<std::string> &getSubtopics();
    void resplitTopic();
};

class WillPublish : public Publish
{
    bool isQueued = false;
    std::chrono::time_point<std::chrono::steady_clock> queuedAt;
public:
    uint32_t will_delay = 0;
    WillPublish() = default;
    WillPublish(const Publish &other);
    void setQueuedAt();
    uint32_t getQueuedAtAge() const;
    std::optional<Mqtt5PropertyBuilder> getPropertyBuilder() const;
};

class PubResponse
{
public:
    PubResponse(const PubResponse &other) = delete;
    PubResponse(const ProtocolVersion protVersion, const PacketType packet_type, ReasonCodes reason_code, uint16_t packet_id);

    const PacketType packet_type;
    const ProtocolVersion protocol_version;
    const ReasonCodes reason_code;
    uint16_t packet_id;
    uint8_t getLengthIncludingFixedHeader() const;
    uint8_t getRemainingLength() const;
    bool needsReasonCode() const;
};

class Disconnect
{
public:
    ProtocolVersion protocolVersion;
    ReasonCodes reasonCode;
    std::shared_ptr<Mqtt5PropertyBuilder> propertyBuilder;
    Disconnect(const ProtocolVersion protVersion, ReasonCodes reason_code);
    size_t getLengthWithoutFixedHeader() const;
};

class Auth
{
public:
    ReasonCodes reasonCode;
    std::shared_ptr<Mqtt5PropertyBuilder> propertyBuilder;
    Auth(ReasonCodes reasonCode, const std::string &authMethod, const std::string &authData);
    size_t getLengthWithoutFixedHeader() const;
};

struct Connect
{
    const ProtocolVersion protocolVersion;
    bool clean_start = true;
    bool bridgeProtocolBit = false;
    std::string clientid;
    std::optional<std::string> fmq_client_group_id;
    std::optional<std::string> username;
    std::optional<std::string> password;
    uint16_t keepalive = 60;
    uint32_t sessionExpiryInterval = 0;
    uint16_t maxIncomingTopicAliasValue = 0;
    std::shared_ptr<WillPublish> will;
    std::optional<std::string> authenticationMethod;
    std::optional<std::string> authenticationData;

    Connect(ProtocolVersion protocolVersion, const std::string &clientid);
    std::string_view getMagicString() const;
};

/**
 * @brief The Subscribe struct can be used to construct a mqtt packet of type 'subscribe'.
 *
 * It's rudimentary. Offically you can subscribe to multiple topics at once, but I have no need for that.
 */
struct Subscribe
{
    const ProtocolVersion protocolVersion;
    uint16_t packetId;
    std::string topic;
    uint8_t qos;
    bool noLocal = false;
    bool retainAsPublished = false;
    uint32_t subscriptionIdentifier = 0;
    RetainHandling retainHandling = RetainHandling::SendRetainedMessagesAtSubscribe;

    Subscribe(const ProtocolVersion protocolVersion, uint16_t packetId, const std::string &topic, uint8_t qos);
};

/**
 * @brief The Unsubscribe struct can be used to construct a mqtt packet of type 'unsubscribe'.
 *
 * It's rudimentary. Offically you can unsubscribe to multiple topics at once, but I have no need for that.
 */
struct Unsubscribe
{
    const ProtocolVersion protocolVersion;
    uint16_t packetId;
    std::string topic;
    std::shared_ptr<Mqtt5PropertyBuilder> propertyBuilder;

    Unsubscribe(const ProtocolVersion protocolVersion, uint16_t packetId, const std::string &topic);
    size_t getLengthWithoutFixedHeader() const;
};

enum class PacketDropReason
{
    Success,
    ClientError,
    ClientOffline,
    AuthDenied,
    BiggerThanPacketLimit,
    BufferFull,
    QoSTODOSomethingSomething
};

#endif // TYPES_H
