#ifndef MQTTPACKET_H
#define MQTTPACKET_H

#include "unistd.h"
#include <memory>
#include <vector>
#include <exception>

#include "client.h"
#include "exceptions.h"
#include "types.h"

class Client;


class MqttPacket
{
    std::vector<char> bites;
    size_t fixed_header_length = 0;
    Client *sender;
    size_t pos = 0;
    ProtocolVersion protocolVersion = ProtocolVersion::None;

    char *readBytes(size_t length);
    char readByte();
    void writeByte(char b);
    uint16_t readTwoBytesToUInt16();

public:
    PacketType packetType = PacketType::Reserved;
    MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client *sender);
    MqttPacket(const ConnAck &connAck);

    void handle();
    void handleConnect();
    size_t getSize() { return bites.size(); }
    const std::vector<char> &getBites() { return bites; }

};

#endif // MQTTPACKET_H
