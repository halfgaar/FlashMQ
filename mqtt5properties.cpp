/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "mqtt5properties.h"

#include <cstring>
#include <vector>
#include <cassert>
#include <array>

#include "exceptions.h"

Mqtt5PropertyBuilder::Mqtt5PropertyBuilder()
{
    bytes.reserve(128);
}

size_t Mqtt5PropertyBuilder::getLength()
{
    length = bytes.size();
    return length.getLen() + bytes.size();
}

const VariableByteInt &Mqtt5PropertyBuilder::getVarInt()
{
    length = bytes.size();
    return length;
}

const std::vector<char> &Mqtt5PropertyBuilder::getBytes() const
{
    return bytes;
}

void Mqtt5PropertyBuilder::writeServerKeepAlive(uint16_t val)
{
    writeUint16(Mqtt5Properties::ServerKeepAlive, val);
}

void Mqtt5PropertyBuilder::writeSessionExpiry(uint32_t val)
{
    writeUint32(Mqtt5Properties::SessionExpiryInterval, val);
}

void Mqtt5PropertyBuilder::writeReceiveMax(uint16_t val)
{
    writeUint16(Mqtt5Properties::ReceiveMaximum, val);
}

void Mqtt5PropertyBuilder::writeRetainAvailable(uint8_t val)
{
    writeUint8(Mqtt5Properties::RetainAvailable, val);
}

void Mqtt5PropertyBuilder::writeMaxPacketSize(uint32_t val)
{
    writeUint32(Mqtt5Properties::MaximumPacketSize, val);
}

void Mqtt5PropertyBuilder::writeAssignedClientId(const std::string &clientid)
{
    writeStr(Mqtt5Properties::AssignedClientIdentifier, clientid);
}

void Mqtt5PropertyBuilder::writeMaxTopicAliases(uint16_t val)
{
    writeUint16(Mqtt5Properties::TopicAliasMaximum, val);
}

void Mqtt5PropertyBuilder::writeMaxQoS(uint8_t qos)
{
    writeUint8(Mqtt5Properties::MaximumQoS, qos);
}

void Mqtt5PropertyBuilder::writeWildcardSubscriptionAvailable(uint8_t val)
{
    writeUint8(Mqtt5Properties::WildcardSubscriptionAvailable, val);
}

void Mqtt5PropertyBuilder::writeSubscriptionIdentifier(uint32_t val)
{
    writeVariableByteInt(Mqtt5Properties::SubscriptionIdentifier, val);
}

void Mqtt5PropertyBuilder::writeSubscriptionIdentifiersAvailable(uint8_t val)
{
    writeUint8(Mqtt5Properties::SubscriptionIdentifierAvailable, val);
}

void Mqtt5PropertyBuilder::writeSharedSubscriptionAvailable(uint8_t val)
{
    writeUint8(Mqtt5Properties::SharedSubscriptionAvailable, val);
}

void Mqtt5PropertyBuilder::writeContentType(const std::string &format)
{
    writeStr(Mqtt5Properties::ContentType, format);
}

void Mqtt5PropertyBuilder::writePayloadFormatIndicator(uint8_t val)
{
    writeUint8(Mqtt5Properties::PayloadFormatIndicator, val);
}

void Mqtt5PropertyBuilder::writeMessageExpiryInterval(uint32_t val)
{
    writeUint32(Mqtt5Properties::MessageExpiryInterval, val);
}

void Mqtt5PropertyBuilder::writeResponseTopic(const std::string &str)
{
    writeStr(Mqtt5Properties::ResponseTopic, str);
}

void Mqtt5PropertyBuilder::writeUserProperties(const std::vector<std::pair<std::string, std::string>> &properties)
{
    for (auto &p : properties)
    {
        writeUserProperty(p.first, p.second);
    }
}

void Mqtt5PropertyBuilder::writeUserProperty(const std::string &key, const std::string &value)
{
    if (this->userPropertyCount++ > 50)
        throw ProtocolError("Trying to set more than 50 user properties. Likely a bad actor.", ReasonCodes::ImplementationSpecificError);

    write2Str(Mqtt5Properties::UserProperty, key, value);
}

void Mqtt5PropertyBuilder::writeCorrelationData(const std::string &correlationData)
{
    writeStr(Mqtt5Properties::CorrelationData, correlationData);
}

void Mqtt5PropertyBuilder::writeTopicAlias(const uint16_t id)
{
    writeUint16(Mqtt5Properties::TopicAlias, id);
}

void Mqtt5PropertyBuilder::writeAuthenticationMethod(const std::string &method)
{
    writeStr(Mqtt5Properties::AuthenticationMethod, method);
}

void Mqtt5PropertyBuilder::writeAuthenticationData(const std::string &data)
{
    writeStr(Mqtt5Properties::AuthenticationData, data);
}

void Mqtt5PropertyBuilder::writeWillDelay(uint32_t delay)
{
    writeUint32(Mqtt5Properties::WillDelayInterval, delay);
}

void Mqtt5PropertyBuilder::writeUint32(Mqtt5Properties prop, const uint32_t x)
{
    size_t pos = bytes.size();
    const size_t newSize = pos + 5;
    bytes.resize(newSize);

    const uint8_t a = static_cast<uint8_t>(x >> 24);
    const uint8_t b = static_cast<uint8_t>(x >> 16);
    const uint8_t c = static_cast<uint8_t>(x >> 8);
    const uint8_t d = static_cast<uint8_t>(x);

    bytes[pos++] = static_cast<uint8_t>(prop);
    bytes[pos++] = a;
    bytes[pos++] = b;
    bytes[pos++] = c;
    bytes[pos] = d;
}

void Mqtt5PropertyBuilder::writeUint16(Mqtt5Properties prop, const uint16_t x)
{
    size_t pos = bytes.size();
    const size_t newSize = pos + 3;
    bytes.resize(newSize);

    const uint8_t a = static_cast<uint8_t>(x >> 8);
    const uint8_t b = static_cast<uint8_t>(x);

    bytes[pos++] = static_cast<uint8_t>(prop);
    bytes[pos++] = a;
    bytes[pos] = b;
}

void Mqtt5PropertyBuilder::writeUint8(Mqtt5Properties prop, const uint8_t x)
{
    size_t pos = bytes.size();
    const size_t newSize = pos + 2;
    bytes.resize(newSize);

    bytes[pos++] = static_cast<uint8_t>(prop);
    bytes[pos] = x;
}

void Mqtt5PropertyBuilder::writeStr(Mqtt5Properties prop, const std::string &str)
{
    if (str.length() > 65535)
        throw ProtocolError("String too long.", ReasonCodes::MalformedPacket);

    const uint16_t strlen = str.length();

    size_t pos = bytes.size();
    const size_t newSize = pos + strlen + 3;
    bytes.resize(newSize);

    const uint8_t a = static_cast<uint8_t>(strlen >> 8);
    const uint8_t b = static_cast<uint8_t>(strlen);

    bytes[pos++] = static_cast<uint8_t>(prop);
    bytes[pos++] = a;
    bytes[pos++] = b;

    std::memcpy(&bytes[pos], str.c_str(), strlen);
}

void Mqtt5PropertyBuilder::write2Str(Mqtt5Properties prop, const std::string &one, const std::string &two)
{
    size_t pos = bytes.size();
    const size_t newSize = pos + one.length() + two.length() + 5;
    bytes.resize(newSize);

    bytes[pos++] = static_cast<uint8_t>(prop);

    std::array<const std::string*, 2> strings;
    strings[0] = &one;
    strings[1] = &two;

    for (const std::string *str : strings)
    {
        if (str->length() > 0xFFFF)
            throw ProtocolError("String too long.", ReasonCodes::MalformedPacket);

        const uint16_t strlen = str->length();

        const uint8_t a = static_cast<uint8_t>(strlen >> 8);
        const uint8_t b = static_cast<uint8_t>(strlen);

        bytes[pos++] = a;
        bytes[pos++] = b;

        std::memcpy(&bytes[pos], str->c_str(), strlen);
        pos += strlen;
    }
}

void Mqtt5PropertyBuilder::writeVariableByteInt(Mqtt5Properties prop, const uint32_t val)
{
    const VariableByteInt x(val);

    size_t pos = bytes.size();
    const size_t newSize = pos + x.getLen() + 1;
    bytes.resize(newSize);

    bytes[pos++] = static_cast<uint8_t>(prop);

    std::memcpy(&bytes[pos], x.data(), x.getLen());
}

