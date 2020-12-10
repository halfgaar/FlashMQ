#ifndef TYPES_H
#define TYPES_H

#include "stdint.h"
#include <list>
#include <string>

enum class PacketType
{
    Reserved = 0,
    CONNECT = 1,
    CONNACK = 2,
    PUBLISH = 3,
    PUBACK = 4,
    PUBREC = 5,
    PUBREL = 6,
    PUBCOMP = 7,
    SUBSCRIBE = 8,
    SUBACK = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK = 11,
    PINGREQ = 12,
    PINGRESP = 13,
    DISCONNECT = 14,

    Reserved2 = 15
};

enum class ProtocolVersion
{
    None = 0,
    Mqtt31 = 0x03,
    Mqtt311 = 0x04
};

enum class ConnAckReturnCodes
{
    Accepted = 0,
    UnacceptableProtocolVersion = 1,
    ClientIdRejected = 2,
    ServerUnavailable = 3,
    MalformedUsernameOrPassword = 4,
    NotAuthorized = 5
};

class ConnAck
{
public:
    ConnAck(ConnAckReturnCodes return_code);
    ConnAckReturnCodes return_code;
};

enum class SubAckReturnCodes
{
    MaxQoS0 = 0,
    MaxQoS1 = 1,
    MaxQoS2 = 2,
    Fail = 0x80
};

class SubAck
{
public:
    uint16_t packet_id;
    std::list<SubAckReturnCodes> responses;
    SubAck(uint16_t packet_id, const std::list<std::string> &subs);
};

class Publish
{
public:
    std::string topic;
    std::string payload;
    Publish(std::string &topic, std::string payload);
};

#endif // TYPES_H
