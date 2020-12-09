#ifndef TYPES_H
#define TYPES_H

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

#endif // TYPES_H
