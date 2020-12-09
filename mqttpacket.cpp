#include "mqttpacket.h"
#include <cstring>

MqttPacket::MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client *sender) : // TODO: length of remaining length
    bites(len),
    fixed_header_length(fixed_header_length),
    sender(sender)
{
    unsigned char _packetType = buf[0] >> 4;
    packetType = (PacketType)_packetType;
    pos += fixed_header_length;

    std::memcpy(&bites[0], buf, len);
}

void MqttPacket::handle()
{
    if (packetType == PacketType::CONNECT)
        handleConnect();
}

void MqttPacket::handleConnect()
{
    if (sender->hasConnectPacketSeen())
        throw ProtocolError("Client already sent a CONNECT.");

    // TODO: Do all packets have a variable header?
    variable_header_length = readTwoBytesToUInt16();

    if (variable_header_length == 4)
    {
        char *c = readBytes(variable_header_length);
        std::string magic_marker(c, variable_header_length);

        char protocol_level = readByte();

        if (magic_marker == "MQTT" && protocol_level == 0x04)
        {
            protocolVersion = ProtocolVersion::Mqtt311;

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

            sender->setClientProperties(clientid, username, true);

        }
    }
    else if (variable_header_length == 6)
    {
        throw ProtocolError("Only MQTT 3.1.1 implemented.");
    }

    throw ProtocolError("Unprogrammed sequence in CONNECT.");
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

uint16_t MqttPacket::readTwoBytesToUInt16()
{
    if (pos + 2 > bites.size())
        throw ProtocolError("Invalid packet: header specifies invalid length.");

    uint16_t i = bites[pos] << 8 | bites[pos+1];
    pos += 2;
    return i;
}



std::string MqttPacket::getClientId()
{
    if (packetType != PacketType::CONNECT)
        throw ProtocolError("Can't get clientid from non-connect packet.");

    uint16_t clientid_length = (bites[fixed_header_length + 10] << 8) | (bites[fixed_header_length + 11]);
    size_t client_id_start = fixed_header_length + 12;

    if (clientid_length + 12 < bites.size())
    {
        std::string result(&bites[client_id_start], clientid_length);
        return result;
    }

    throw ProtocolError("Can't get clientid");
}





