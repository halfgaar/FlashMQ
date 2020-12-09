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
    bool valid = false;

    std::vector<char> bites;
    const size_t fixed_header_length;
    uint16_t variable_header_length;
    Client *sender;
    std::string clientid;
    size_t pos = 0;
    ProtocolVersion protocolVersion = ProtocolVersion::None;
public:
    PacketType packetType = PacketType::Reserved;
    MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client *sender);

    bool isValid() { return valid; }
    std::string getClientId();
    void handle();
    void handleConnect();
    char *readBytes(size_t length);
    char readByte();
    uint16_t readTwoBytesToUInt16();
};

#endif // MQTTPACKET_H
