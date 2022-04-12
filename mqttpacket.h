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

#ifndef MQTTPACKET_H
#define MQTTPACKET_H

#include "unistd.h"
#include <memory>
#include <vector>
#include <exception>

#include "forward_declarations.h"

#include "client.h"
#include "exceptions.h"
#include "types.h"
#include "subscriptionstore.h"
#include "cirbuf.h"
#include "logger.h"
#include "mainapp.h"

#include "variablebyteint.h"
#include "mqtt5properties.h"

/**
 * @brief The MqttPacket class represents incoming and outgonig packets.
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

    // It's important to understand that this class is used for incoming packets as well as new outgoing packets. When we create
    // new outgoing packets, we generally know exactly who it's for and the information is only stored in this->bites. So, the
    // publishData and fields like hasTopicAlias are invalid in those cases.
    bool externallyReceived = false;

    Logger *logger = Logger::getInstance();

    char *readBytes(size_t length);
    char readByte();
    void writeByte(char b);
    void writeUint16(uint16_t x);
    void writeBytes(const char *b, size_t len);
    void writeProperties(const std::shared_ptr<Mqtt5PropertyBuilder> &properties);
    void writeVariableByteInt(const VariableByteInt &v);
    uint16_t readTwoBytesToUInt16();
    uint32_t readFourBytesToUint32();
    size_t remainingAfterPos();
    size_t decodeVariableByteIntAtPos();
    void readUserProperty();

    void calculateRemainingLength();

    MqttPacket(const MqttPacket &other) = delete;
public:
    PacketType packetType = PacketType::Reserved;
    MqttPacket(CirBuf &buf, size_t packet_len, size_t fixed_header_length, std::shared_ptr<Client> &sender); // Constructor for parsing incoming packets.

    MqttPacket(MqttPacket &&other) = default;

    size_t setClientSpecificPropertiesAndGetRequiredSizeForPublish(const ProtocolVersion protocolVersion, Publish &publishData) const;

    // Constructor for outgoing packets. These may not allocate room for the fixed header, because we don't (always) know the length in advance.
    MqttPacket(const ConnAck &connAck);
    MqttPacket(const SubAck &subAck);
    MqttPacket(const UnsubAck &unsubAck);
    MqttPacket(const ProtocolVersion protocolVersion, Publish &_publish);
    MqttPacket(const PubResponse &pubAck);
    MqttPacket(const Disconnect &disconnect);

    static void bufferToMqttPackets(CirBuf &buf, std::vector<MqttPacket> &packetQueueIn, std::shared_ptr<Client> &sender);

    void handle();
    void handleConnect();
    void handleDisconnect();
    void handleSubscribe();
    void handleUnsubscribe();
    void handlePing();
    void parsePublishData();
    void handlePublish();
    void handlePubAck();
    void handlePubRec();
    void handlePubRel();
    void handlePubComp();

    uint8_t getFixedHeaderLength() const;
    size_t getSizeIncludingNonPresentHeader() const;
    const std::vector<char> &getBites() const { return bites; }
    char getQos() const { return publishData.qos; }
    void setQos(const char new_qos);
    ProtocolVersion getProtocolVersion() const { return protocolVersion;}
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics() const;
    std::shared_ptr<Client> getSender() const;
    void setSender(const std::shared_ptr<Client> &value);
    bool containsFixedHeader() const;
    void setPacketId(uint16_t packet_id);
    uint16_t getPacketId() const;
    void setDuplicate();
    void readIntoBuf(CirBuf &buf) const;
    std::string getPayloadCopy() const;
    bool getRetain() const;
    void setRetain();
    const Publish &getPublishData();
    bool containsClientSpecificProperties() const;
    const std::vector<std::pair<std::string, std::string>> *getUserProperties() const;
};

#endif // MQTTPACKET_H
