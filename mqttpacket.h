/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef MQTTPACKET_H
#define MQTTPACKET_H

#include <unistd.h>
#include <memory>
#include <vector>

#include "forward_declarations.h"

#include "types.h"
#include "cirbuf.h"
#include "logger.h"

#include "variablebyteint.h"
#include "mqtt5properties.h"
#include "packetdatatypes.h"
#include "exceptions.h"

enum class HandleResult
{
    Done,
    Defer
};

/**
 * @brief The MqttPacket class represents incoming and outgoing packets.
 *
 * Be sure to understand the 'externallyReceived' member. See in-code documentation.
 */
class MqttPacket
{
#ifdef TESTING
    friend class MainTests;
#endif

    std::vector<char> bites;
    Publish publishData;
    size_t fixed_header_length = 0; // if 0, this packet does not contain the bytes of the fixed header.
    VariableByteInt remainingLength;
    char first_byte = 0;
    size_t pos = 0;
    size_t packet_id_pos = 0;
    uint16_t packet_id = 0;
    ProtocolVersion protocolVersion = ProtocolVersion::None;
    size_t payloadStart = 0;
    size_t payloadLen = 0;
    bool dontReuseBites = false;

    // It's important to understand that this class is used for incoming packets as well as new outgoing packets. When we create
    // new outgoing packets, we generally know exactly who it's for and the information is only stored in this->bites. So, the
    // publishData and fields like hasTopicAlias are invalid in those cases.
    bool externallyReceived = false;

    Logger *logger = Logger::getInstance();

    std::string_view readBytes(size_t length);
    char readByte();
    uint8_t readUint8();
    void writeByte(char b);
    void writeUint16(uint16_t x);

    template<typename T>
    void write(T &src)
    {
        if (pos + src.size() > bites.size())
            throw ProtocolError("Exceeding packet size", ReasonCodes::MalformedPacket);

        std::copy(src.begin(), src.end(), bites.begin() + pos);
        pos += src.size();
    }

    template<typename T>
    void writeProperties(T properties)
    {
        if (!properties)
        {
            writeByte(0);
            return;
        }

        writeProperties(*properties);
    }

    void writeProperties(Mqtt5PropertyBuilder &properties);
    void writeVariableByteInt(const VariableByteInt &v);
    void writeString(const std::string &s);
    void writeString(std::string_view s);
    uint16_t readTwoBytesToUInt16();
    uint32_t readFourBytesToUint32();
    size_t remainingAfterPos();
    size_t decodeVariableByteIntAtPos();
    std::string readBytesToString(const uint16_t maxLength = std::numeric_limits<uint16_t>::max(), bool validateUtf8 = true, bool alsoCheckInvalidPublishChars = false);

    void calculateRemainingLength();
    void setPosToDataStart();
    bool atEnd() const;

#ifndef TESTING
    // In production, I want to be sure I don't accidentally copy packets, because it's slow.
    MqttPacket(const MqttPacket &other) = delete;
#endif
public:
#ifdef TESTING
    // In testing I need to copy packets for administrative purposes.
    MqttPacket(const MqttPacket &other) = default;
#endif
    PacketType packetType = PacketType::Reserved;

    MqttPacket(std::vector<char> &&packet_bytes, size_t fixed_header_length, std::shared_ptr<Client> &sender); // Constructor for parsing incoming packets.
    MqttPacket(MqttPacket &&other) = default;

    // Constructor for outgoing packets. These may not allocate room for the fixed header, because we don't (always) know the length in advance.
    MqttPacket(const ConnAck &connAck);
    MqttPacket(const SubAck &subAck);
    MqttPacket(const UnsubAck &unsubAck);
    MqttPacket(const ProtocolVersion protocolVersion, const Publish &_publish);
    MqttPacket(const ProtocolVersion protocolVersion, const Publish &_publish, const uint8_t _qos, const uint16_t _topic_alias,
               const bool _skip_topic, const uint32_t subscriptionIdentifier, const std::optional<std::string> &topic_override);
    MqttPacket(const PubResponse &pubAck);
    MqttPacket(const Disconnect &disconnect);
    MqttPacket(const Auth &auth);
    MqttPacket(const Connect &connect);
    MqttPacket(const ProtocolVersion protocolVersion, const uint16_t packetId, const uint32_t subscriptionIdentifier, const std::vector<Subscribe> &subscriptions);
    MqttPacket(const ProtocolVersion protocolVersion, const uint16_t packetId, const std::vector<Unsubscribe> &unsubs);

    static void bufferToMqttPackets(CirBuf &buf, std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender);

    HandleResult handle(std::shared_ptr<Client> &sender);
    AuthPacketData parseAuthData();
    ConnectData parseConnectData(std::shared_ptr<Client> &sender);
    ConnAckData parseConnAckData();
    void handleConnect(std::shared_ptr<Client> &sender);
    void handleConnAck(std::shared_ptr<Client> &sender);
    void handleExtendedAuth(std::shared_ptr<Client> &sender);
    DisconnectData parseDisconnectData();
    void handleDisconnect(std::shared_ptr<Client> &sender);
    void handleSubscribe(std::shared_ptr<Client> &sender);
    void handleSubAck(std::shared_ptr<Client> &sender);
    void handleUnsubscribe(std::shared_ptr<Client> &sender);
    void handlePing(std::shared_ptr<Client> &sender);
    void parsePublishData(std::shared_ptr<Client> &sender);
    void handlePublish(std::shared_ptr<Client> &sender);
    void parsePubAckData();
    void handlePubAck(std::shared_ptr<Client> &sender);
    PubRecData parsePubRecData();
    void handlePubRec(std::shared_ptr<Client> &sender);
    void parsePubRelData();
    void handlePubRel(std::shared_ptr<Client> &sender);
    void parsePubComp();
    void handlePubComp(std::shared_ptr<Client> &sender);
    SubAckData parseSubAckData();

    uint8_t getFixedHeaderLength() const;
    size_t getSizeIncludingNonPresentHeader() const;
    uint8_t getQos() const { return publishData.qos; }
    void setQos(const uint8_t new_qos);
    ProtocolVersion getProtocolVersion() const { return protocolVersion;}
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics();
    bool containsFixedHeader() const;
    void setPacketId(uint16_t packet_id);
    uint16_t getPacketId() const;
    void setDuplicate();
    void readIntoBuf(CirBuf &buf) const;
    std::string getPayloadCopy() const;
    std::string_view getPayloadView() const;
    bool getRetain() const;
    void setRetain(bool val);
    const Publish &getPublishData();
    bool biteArrayCannotBeReused() const;
    std::vector<std::pair<std::string, std::string>> *getUserProperties() const;
    const std::optional<std::string> &getCorrelationData() const;
    const std::optional<std::string> &getResponseTopic() const;
    const std::optional<std::string> &getContentType() const;
};

struct SubscriptionTuple
{
    const std::string topic;
    const std::vector<std::string> subtopics;
    const uint8_t qos;
    const bool noLocal;
    const bool retainAsPublished;
    const std::string shareName;
    const AuthResult authResult;
    const uint32_t subscriptionIdentifier = 0;
    const RetainHandling retainHandling = RetainHandling::SendRetainedMessagesAtSubscribe;

    SubscriptionTuple(const std::string &topic, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished,
                      const std::string &shareName, const AuthResult authResult, const uint32_t subscriptionIdentifier,
                      const RetainHandling retainHandling);
};

#endif // MQTTPACKET_H
