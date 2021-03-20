/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

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
    ConnAck(ConnAckReturnCodes return_code, bool session_present=false);
    ConnAckReturnCodes return_code;
    bool session_present = false;
    size_t getLengthWithoutFixedHeader() const { return 2;} // size of connack is always the same
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
    SubAck(uint16_t packet_id, const std::list<char> &subs_qos_reponses);
    size_t getLengthWithoutFixedHeader() const;
};

class UnsubAck
{
public:
    uint16_t packet_id;
    UnsubAck(uint16_t packet_id);
    size_t getLengthWithoutFixedHeader() const;
};

class Publish
{
public:
    std::string topic;
    std::string payload;
    char qos = 0;
    bool retain = false; // Note: existing subscribers don't get publishes of retained messages with retain=1. [MQTT-3.3.1-9]
    Publish(const std::string &topic, const std::string payload, char qos);
    size_t getLengthWithoutFixedHeader() const;
};

class PubAck
{
public:
    PubAck(uint16_t packet_id);
    uint16_t packet_id;
    size_t getLengthWithoutFixedHeader() const;
};

#endif // TYPES_H
