#ifndef TYPES_H
#define TYPES_H

enum class PacketType
{
    Reserved = 0,
    CONNECT = 1,
    CONNACK = 2,

    Reserved2 = 15
};

enum class ProtocolVersion
{
    None = 0,
    Mqtt31 = 0x03,
    Mqtt311 = 0x04
};

#endif // TYPES_H
