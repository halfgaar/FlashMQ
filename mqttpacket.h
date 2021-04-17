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

struct RemainingLength
{
    char bytes[4];
    int len = 0;
public:
    RemainingLength();
};

class MqttPacket
{
    std::string topic;
    std::vector<std::string> subtopics;
    std::vector<char> bites;
    size_t fixed_header_length = 0; // if 0, this packet does not contain the bytes of the fixed header.
    RemainingLength remainingLength;
    char qos = 0;
    std::shared_ptr<Client> sender;
    char first_byte = 0;
    size_t pos = 0;
    size_t packet_id_pos = 0;
    ProtocolVersion protocolVersion = ProtocolVersion::None;
    Logger *logger = Logger::getInstance();

    char *readBytes(size_t length);
    char readByte();
    void writeByte(char b);
    void writeBytes(const char *b, size_t len);
    uint16_t readTwoBytesToUInt16();
    size_t remainingAfterPos();

    void calculateRemainingLength();

    MqttPacket(const MqttPacket &other) = default;
public:
    PacketType packetType = PacketType::Reserved;
    MqttPacket(CirBuf &buf, size_t packet_len, size_t fixed_header_length, std::shared_ptr<Client> &sender); // Constructor for parsing incoming packets.

    MqttPacket(MqttPacket &&other) = default;

    std::shared_ptr<MqttPacket> getCopy() const;

    // Constructor for outgoing packets. These may not allocate room for the fixed header, because we don't (always) know the length in advance.
    MqttPacket(const ConnAck &connAck);
    MqttPacket(const SubAck &subAck);
    MqttPacket(const UnsubAck &unsubAck);
    MqttPacket(const Publish &publish);
    MqttPacket(const PubAck &pubAck);

    void handle();
    void handleConnect();
    void handleDisconnect();
    void handleSubscribe();
    void handleUnsubscribe();
    void handlePing();
    void handlePublish();
    void handlePubAck();

    size_t getSizeIncludingNonPresentHeader() const;
    const std::vector<char> &getBites() const { return bites; }
    char getQos() const { return qos; }
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics() const;
    std::shared_ptr<Client> getSender() const;
    void setSender(const std::shared_ptr<Client> &value);
    bool containsFixedHeader() const;
    char getFirstByte() const;
    RemainingLength getRemainingLength() const;
    void setPacketId(uint16_t packet_id);
    void setDuplicate();
    size_t getTotalMemoryFootprint();
};

#endif // MQTTPACKET_H
