#ifndef BYTESTOPACKETPARSER_H
#define BYTESTOPACKETPARSER_H

#include <unistd.h>
#include <vector>

#include "mqttpacket.h"

#define MQTT_HEADER_LENGH 2

class BytesToPacketParser
{
public:
    BytesToPacketParser();

    size_t bytesToPackets(char *buf, size_t len, std::vector<MqttPacket> &queue);
};

#endif // BYTESTOPACKETPARSER_H
