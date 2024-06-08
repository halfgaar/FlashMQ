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
#include <exception>

#include "forward_declarations.h"

#include "types.h"
#include "cirbuf.h"
#include "logger.h"

#include "variablebyteint.h"
#include "mqtt5properties.h"
#include "packetdatatypes.h"

/**
 * @brief The MqttPacket class represents incoming and outgoing packets.
 *
 * Be sure to understand the 'externallyReceived' member. See in-code documentation.
 *
 * TODO: I could perhaps make a subclass for the externally received one.
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
    std::shared_ptr<Client> sender;
    char first_byte = 0;
    size_t pos = 0;
    size_t packet_id_pos = 0;
    uint16_t packet_id = 0;
    ProtocolVersion protocolVersion = ProtocolVersion::None;
    size_t payloadStart = 0;
    size_t payloadLen = 0;
    bool hasTopicAlias = false;
    bool alteredByPlugin = false;

    // It's important to understand that this class is used for incoming packets as well as new outgoing packets. When we create
    // new outgoing packets, we generally know exactly who it's for and the information is only stored in this->bites. So, the
    // publishData and fields like hasTopicAlias are invalid in those cases.
    bool externallyReceived = false;

    Logger *logger = Logger::getInstance();

    void advancePos(size_t len);
    char *readBytes(size_t length);
    char readByte();
    uint8_t readUint8();
    void writeByte(char b);
    void writeUint16(uint16_t x);
    void writeBytes(const char *b, size_t len);
    void writeProperties(const std::shared_ptr<Mqtt5PropertyBuilder> &properties);
    void joinAndWriteProperties(const std::shared_ptr<const Mqtt5PropertyBuilder> &properties);
    void writeVariableByteInt(const VariableByteInt &v);
    void writeString(const std::string &s);
    void writeString(std::string_view s);
    uint16_t readTwoBytesToUInt16();
    uint32_t readFourBytesToUint32();
    size_t remainingAfterPos();
    size_t decodeVariableByteIntAtPos();
    void readUserProperty();
    std::string readBytesToString(bool validateUtf8 = true, bool alsoCheckInvalidPublishChars = false);

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

    MqttPacket(CirBuf &buf, size_t packet_len, size_t fixed_header_length, std::shared_ptr<Client> &sender); // Constructor for parsing incoming packets.
    MqttPacket(MqttPacket &&other) = default;

    // Constructor for outgoing packets. These may not allocate room for the fixed header, because we don't (always) know the length in advance.
    MqttPacket(const ConnAck &connAck);
    MqttPacket(const SubAck &subAck);
    MqttPacket(const UnsubAck &unsubAck);
    MqttPacket(const ProtocolVersion protocolVersion, const Publish &_publish);
    MqttPacket(const ProtocolVersion protocolVersion, const Publish &_publish, const uint8_t _qos, const uint16_t _topic_alias, const bool _skip_topic);
    MqttPacket(const PubResponse &pubAck);
    MqttPacket(const Disconnect &disconnect);
    MqttPacket(const Auth &auth);
    MqttPacket(const Connect &connect);
    MqttPacket(const Subscribe &subscribe);
    MqttPacket(const Unsubscribe &unsubscribe);

    static void bufferToMqttPackets(CirBuf &buf, std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender);

    void handle();
    AuthPacketData parseAuthData();
    ConnectData parseConnectData();
    ConnAckData parseConnAckData();
    void handleConnect();
    void handleConnAck();
    void handleExtendedAuth();
    DisconnectData parseDisconnectData();
    void handleDisconnect();
    void handleSubscribe();
    void handleSubAck();
    void handleUnsubscribe();
    void handlePing();
    void parsePublishData();
    void handlePublish();
    void parsePubAckData();
    void handlePubAck();
    PubRecData parsePubRecData();
    void handlePubRec();
    void parsePubRelData();
    void handlePubRel();
    void parsePubComp();
    void handlePubComp();
    SubAckData parseSubAckData();

    uint8_t getFixedHeaderLength() const;
    size_t getSizeIncludingNonPresentHeader() const;
    const std::vector<char> &getBites() const { return bites; }
    uint8_t getQos() const { return publishData.qos; }
    void setQos(const uint8_t new_qos);
    ProtocolVersion getProtocolVersion() const { return protocolVersion;}
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics();
    std::shared_ptr<Client> getSender() const;
    void setSender(const std::shared_ptr<Client> &value);
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
    bool containsClientSpecificProperties() const;
    bool isAlteredByPlugin() const;
    std::vector<std::pair<std::string, std::string>> *getUserProperties();
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

    SubscriptionTuple(const std::string &topic, const std::vector<std::string> &subtopics, uint8_t qos, bool noLocal, bool retainAsPublished,
                      const std::string &shareName, const AuthResult authResult);
};

#endif // MQTTPACKET_H
