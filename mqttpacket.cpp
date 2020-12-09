#include "mqttpacket.h"
#include <cstring>
#include <iostream>

MqttPacket::MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client *sender) :
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

void MqttPacket::handle()
{
    if (packetType == PacketType::CONNECT)
        handleConnect();
    else if (packetType == PacketType::PINGREQ)
        std::cout << "PING" << std::endl;
    else if (packetType == PacketType::SUBSCRIBE)
        std::cout << "Sub" << std::endl;
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









