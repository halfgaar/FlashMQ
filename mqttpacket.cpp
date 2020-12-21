#include "mqttpacket.h"
#include <cstring>
#include <iostream>
#include <list>
#include <cassert>

RemainingLength::RemainingLength()
{
    memset(bytes, 0, 4);
}

// constructor for parsing incoming packets
MqttPacket::MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client_p &sender) :
    bites(len),
    fixed_header_length(fixed_header_length),
    sender(sender)
{
    std::memcpy(&bites[0], buf, len);
    first_byte = bites[0];
    unsigned char _packetType = (first_byte & 0xF0) >> 4;
    packetType = (PacketType)_packetType;
    pos += fixed_header_length;
}

// This constructor cheats and doesn't use calculateRemainingLength, because it's always the same. It allocates enough space in the vector.
MqttPacket::MqttPacket(const ConnAck &connAck) :
    bites(connAck.getLength() + 2)
{
    fixed_header_length = 2;
    packetType = PacketType::CONNACK;
    char first_byte = static_cast<char>(packetType) << 4;
    writeByte(first_byte);
    writeByte(2); // length is always 2.
    writeByte(0); // all connect-ack flags are 0, except session-present, but we don't have that yet.
    writeByte(static_cast<char>(connAck.return_code));

}

MqttPacket::MqttPacket(const SubAck &subAck) :
    bites(3)
{
    fixed_header_length = 2; // TODO: this is wrong, pending implementation of the new method in SubAck
    packetType = PacketType::SUBACK;
    char first_byte = static_cast<char>(packetType) << 4;
    writeByte(first_byte);
    writeByte((subAck.packet_id & 0xF0) >> 8);
    writeByte(subAck.packet_id & 0x0F);

    std::vector<char> returnList;
    for (SubAckReturnCodes code : subAck.responses)
    {
        returnList.push_back(static_cast<char>(code));
    }

    bites.insert(bites.end(), returnList.begin(), returnList.end());
    bites[1] = returnList.size() + 1; // TODO: make some generic way of calculating the header and use the multi-byte length
}

MqttPacket::MqttPacket(const Publish &publish) :
    bites(publish.getLength())
{
    if (publish.topic.length() > 0xFFFF)
    {
        throw ProtocolError("Topic path too long.");
    }

    packetType = PacketType::PUBLISH;
    first_byte = static_cast<char>(packetType) << 4;
    first_byte |= (publish.qos << 1);
    first_byte |= (static_cast<char>(publish.retain) & 0b00000001);

    char topicLenMSB = (publish.topic.length() & 0xF0) >> 8;
    char topicLenLSB = publish.topic.length() & 0x0F;
    writeByte(topicLenMSB);
    writeByte(topicLenLSB);
    writeBytes(publish.topic.c_str(), publish.topic.length());

    if (publish.qos)
    {
        throw NotImplementedException("I would write two bytes containing the packet id here, but QoS is not done yet.");
    }

    writeBytes(publish.payload.c_str(), publish.payload.length());
    calculateRemainingLength();
}

void MqttPacket::handle(std::shared_ptr<SubscriptionStore> &subscriptionStore)
{
    if (packetType != PacketType::CONNECT)
    {
        if (!sender->getAuthenticated())
        {
            throw ProtocolError("Non-connect packet from non-authenticated client.");
        }
    }

    if (packetType == PacketType::CONNECT)
        handleConnect();
    else if (packetType == PacketType::PINGREQ)
        sender->writePingResp();
    else if (packetType == PacketType::SUBSCRIBE)
        handleSubscribe(subscriptionStore);
    else if (packetType == PacketType::PUBLISH)
        handlePublish(subscriptionStore);
}

void MqttPacket::handleConnect()
{
    if (sender->hasConnectPacketSeen())
        throw ProtocolError("Client already sent a CONNECT.");

    uint16_t variable_header_length = readTwoBytesToUInt16();

    if (variable_header_length == 4 || variable_header_length == 6)
    {
        char *c = readBytes(variable_header_length);
        std::string magic_marker(c, variable_header_length);

        char protocol_level = readByte();

        if (magic_marker == "MQTT" && protocol_level == 0x04)
        {
            protocolVersion = ProtocolVersion::Mqtt311;
        }
        else if (magic_marker == "MQIsdp" && protocol_level == 0x03)
        {
            protocolVersion = ProtocolVersion::Mqtt31;
        }
        else
        {
            ConnAck connAck(ConnAckReturnCodes::UnacceptableProtocolVersion);
            MqttPacket response(connAck);
            sender->setReadyForDisconnect();
            sender->writeMqttPacket(response);
            std::cout << "Rejecting because of invalid protocol version: " << sender->repr() << std::endl;
            return;
        }

        char flagByte = readByte();
        bool reserved = !!(flagByte & 0b00000001);

        if (reserved)
            throw ProtocolError("Protocol demands reserved flag in CONNECT is 0");


        bool user_name_flag = !!(flagByte & 0b10000000);
        bool password_flag = !!(flagByte & 0b01000000);
        bool will_retain = !!(flagByte & 0b00100000);
        char will_qos = (flagByte & 0b00011000) >> 3;
        bool will_flag = !!(flagByte & 0b00000100);
        bool clean_session = !!(flagByte & 0b00000010);

        uint16_t keep_alive = readTwoBytesToUInt16();

        uint16_t client_id_length = readTwoBytesToUInt16();
        std::string client_id(readBytes(client_id_length), client_id_length);

        std::string username;
        std::string password;
        std::string will_topic;
        std::string will_payload;

        if (will_flag)
        {
            uint16_t will_topic_length = readTwoBytesToUInt16();
            will_topic = std::string(readBytes(will_topic_length), will_topic_length);

            uint16_t will_payload_length = readTwoBytesToUInt16();
            will_payload = std::string(readBytes(will_payload_length), will_payload_length);
        }
        if (user_name_flag)
        {
            uint16_t user_name_length = readTwoBytesToUInt16();
            username = std::string(readBytes(user_name_length), user_name_length);
        }
        if (password_flag)
        {
            uint16_t password_length = readTwoBytesToUInt16();
            password = std::string(readBytes(password_length), password_length);
        }

        // The specs don't really say what to do when client id not UTF8, so including here.
        if (!isValidUtf8(client_id) || !isValidUtf8(username) || !isValidUtf8(password))
        {
            ConnAck connAck(ConnAckReturnCodes::MalformedUsernameOrPassword);
            MqttPacket response(connAck);
            sender->setReadyForDisconnect();
            sender->writeMqttPacket(response);
            std::cout << "Client ID, username or passwords has invalid UTF8: " << sender->repr() << std::endl;
            return;
        }

        // In case the client_id ever appears in topics.
        // TODO: make setting?
        if (strContains(client_id, "+") || strContains(client_id, "#"))
        {
            ConnAck connAck(ConnAckReturnCodes::ClientIdRejected);
            MqttPacket response(connAck);
            sender->setReadyForDisconnect();
            sender->writeMqttPacket(response);
            std::cout << "ClientID has + or # in the id: " << sender->repr() << std::endl;
            return;
        }

        sender->setClientProperties(client_id, username, true, keep_alive);
        sender->setWill(will_topic, will_payload, will_retain, will_qos);
        sender->setAuthenticated(true);

        std::cout << "Connect: " << sender->repr() << std::endl;

        ConnAck connAck(ConnAckReturnCodes::Accepted);
        MqttPacket response(connAck);
        sender->writeMqttPacket(response);
    }
    else
    {
        throw ProtocolError("Invalid variable header length. Garbage?");
    }
}

void MqttPacket::handleSubscribe(std::shared_ptr<SubscriptionStore> &subscriptionStore)
{
    uint16_t packet_id = readTwoBytesToUInt16();

    std::list<char> subs_reponse_codes;
    while (remainingAfterPos() > 0)
    {
        uint16_t topicLength = readTwoBytesToUInt16();
        std::string topic(readBytes(topicLength), topicLength);
        char qos = readByte();
        if (qos > 0)
            throw NotImplementedException("QoS not implemented");
        std::cout << sender->repr() << " Subscribed to " << topic << std::endl;
        subscriptionStore->addSubscription(sender, topic);
        subs_reponse_codes.push_back(qos);
    }

    SubAck subAck(packet_id, subs_reponse_codes);
    MqttPacket response(subAck);
    sender->writeMqttPacket(response);
}

void MqttPacket::handlePublish(std::shared_ptr<SubscriptionStore> &subscriptionStore)
{
    uint16_t variable_header_length = readTwoBytesToUInt16();

    if (variable_header_length == 0)
        throw ProtocolError("Empty publish topic");

    bool retain = (first_byte & 0b00000001);
    bool dup = !!(first_byte & 0b00001000);
    char qos = (first_byte & 0b00000110) >> 1;

    if (qos == 3)
        throw ProtocolError("QoS 3 is a protocol violation.");

    // TODO: validate UTF8.
    std::string topic(readBytes(variable_header_length), variable_header_length);

    if (qos)
    {
        throw ProtocolError("Qos not implemented.");
        uint16_t packet_id = readTwoBytesToUInt16();
    }

    if (retain)
    {
        size_t payload_length = remainingAfterPos();
        std::string payload(readBytes(payload_length), payload_length);

        subscriptionStore->setRetainedMessage(topic, payload, qos);
    }

    // Set dup flag to 0, because that must not be propagated [MQTT-3.3.1-3].
    // Existing subscribers don't get retain=1. [MQTT-3.3.1-9]
    bites[0] &= 0b11110110;

    // For the existing clients, we can just write the same packet back out, with our small alterations.
    subscriptionStore->queuePacketAtSubscribers(topic, *this, sender);
}

void MqttPacket::calculateRemainingLength()
{
    assert(fixed_header_length == 0); // because you're not supposed to call this on packet that we already know the length of.

    size_t x = bites.size();

    do
    {
        if (remainingLength.len > 4)
            throw std::runtime_error("Calculated remaining length is longer than 4 bytes.");

        char encodedByte = x % 128;
        x = x / 128;
        if (x > 0)
            encodedByte = encodedByte | 128;
        remainingLength.bytes[remainingLength.len++] = encodedByte;
    }
    while(x > 0);
}

RemainingLength MqttPacket::getRemainingLength() const
{
    assert(remainingLength.len > 0);
    return remainingLength;
}

size_t MqttPacket::getSizeIncludingNonPresentHeader() const
{
    size_t total = bites.size();

    if (fixed_header_length == 0)
    {
        total++;
        total += remainingLength.len;
    }

    return total;
}


Client_p MqttPacket::getSender() const
{
    return sender;
}

void MqttPacket::setSender(const Client_p &value)
{
    sender = value;
}

bool MqttPacket::containsFixedHeader() const
{
    return fixed_header_length > 0;
}

char MqttPacket::getFirstByte() const
{
    return first_byte;
}

char *MqttPacket::readBytes(size_t length)
{
    if (pos + length > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    char *b = &bites[pos];
    pos += length;
    return b;
}

char MqttPacket::readByte()
{
    if (pos + 1 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    char b = bites[pos++];
    return b;
}

void MqttPacket::writeByte(char b)
{
    if (pos + 1 > bites.size())
        throw ProtocolError("Exceeding packet size");

    bites[pos++] = b;
}

void MqttPacket::writeBytes(const char *b, size_t len)
{
    if (pos + len > bites.size())
        throw ProtocolError("Exceeding packet size");

    memcpy(&bites[pos], b, len);
    pos += len;
}

uint16_t MqttPacket::readTwoBytesToUInt16()
{
    if (pos + 2 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    uint16_t i = bites[pos] << 8 | bites[pos+1];
    pos += 2;
    return i;
}

size_t MqttPacket::remainingAfterPos()
{
    return bites.size() - pos;
}











