#include "mqttpacket.h"
#include <cstring>

MqttPacket::MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client *sender) : // TODO: length of remaining length
    bites(len),
    fixed_header_length(fixed_header_length),
    sender(sender)
{
    unsigned char _packetType = buf[0] >> 4;
    packetType = (PacketType)_packetType; // TODO: veryify some other things and set to invalid if doesn't match

    std::memcpy(&bites[0], buf, len);
}

void MqttPacket::handle()
{
    pos += fixed_header_length;

    if (packetType == PacketType::CONNECT)
        handleConnect();
}

void MqttPacket::handleConnect()
{
    // TODO: Do all packets have a variable header?
    variable_header_length = (bites[fixed_header_length] << 8) | (bites[fixed_header_length+1]);
    pos += 2;

    if (variable_header_length == 4)
    {
        char *c = readBytes(variable_header_length);
        std::string magic_marker(c, variable_header_length);

        char protocol_level = readByte();

        if (magic_marker == "MQTT" && protocol_level == 0x04)
        {
            protocolVersion = ProtocolVersion::Mqtt311;
        }
    }
    else if (variable_header_length == 6)
    {
        throw new ProtocolError("Only MQTT 3.1.1 implemented.");
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

    char b = bites[pos];
    pos++;
    return b;
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





