#include "mqttpacket.h"
#include <cstring>
#include <iostream>
#include <list>

MqttPacket::MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client_p &sender) :
    bites(len),
    fixed_header_length(fixed_header_length),
    sender(sender)
{
    unsigned char _packetType = (buf[0] & 0xF0) >> 4;
    packetType = (PacketType)_packetType;
    pos += fixed_header_length;

    std::memcpy(&bites[0], buf, len);
}

MqttPacket::MqttPacket(const ConnAck &connAck) :
    bites(4)
{
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

void MqttPacket::handle(std::shared_ptr<SubscriptionStore> &subscriptionStore)
{
    if (packetType == PacketType::CONNECT)
        handleConnect();
    else if (packetType == PacketType::PINGREQ)
        sender->writePingResp();
    else if (packetType == PacketType::SUBSCRIBE)
        handleSubscribe(subscriptionStore);
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
            throw ProtocolError("Only MQTT 3.1 and 3.1.1 supported.");
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

        if (will_flag)
        {

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

        // TODO: validate UTF8 encoded username/password.

        sender->setClientProperties(client_id, username, true, keep_alive);

        std::cout << "Connect: " << sender->repr() << std::endl;

        ConnAck connAck(ConnAckReturnCodes::Accepted);
        MqttPacket response(connAck);
        sender->writeMqttPacket(response);
        sender->writeBufIntoFd();
    }
    else
    {
        throw ProtocolError("Invalid variable header length. Garbage?");
    }
}

void MqttPacket::handleSubscribe(std::shared_ptr<SubscriptionStore> &subscriptionStore)
{
    uint16_t packet_id = readTwoBytesToUInt16();

    std::list<std::string> subs; // TODO: list of tuples, probably
    while (remainingAfterPos() > 1)
    {
        uint16_t topicLength = readTwoBytesToUInt16();
        std::string topic(readBytes(topicLength), topicLength);
        std::cout << sender->repr() << " Subscribed to " << topic << std::endl;
        subscriptionStore->addSubscription(sender, topic);
        subs.push_back(std::move(topic));
    }

    char flags = readByte();

    SubAck subAck(packet_id, subs);
    MqttPacket response(subAck);
    sender->writeMqttPacket(response);
    sender->writeBufIntoFd();
}


Client_p MqttPacket::getSender() const
{
    return sender;
}

void MqttPacket::setSender(const Client_p &value)
{
    sender = value;
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









