#include "mqtt5properties.h"

#include "cstring"

#include "exceptions.h"

Mqtt5PropertyBuilder::Mqtt5PropertyBuilder()
{
    bites.reserve(128);
}

size_t Mqtt5PropertyBuilder::getLength() const
{
    return length.getLen() + bites.size();
}

const VariableByteInt &Mqtt5PropertyBuilder::getVarInt() const
{
    return length;
}

const std::vector<char> &Mqtt5PropertyBuilder::getBites() const
{
    return bites;
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

void Mqtt5PropertyBuilder::writeWildcardSubscriptionAvailable(uint8_t val)
{
    writeUint8(Mqtt5Properties::WildcardSubscriptionAvailable, val);
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

void Mqtt5PropertyBuilder::writeUint32(Mqtt5Properties prop, const uint32_t x)
{
    size_t pos = bites.size();
    const size_t newSize = pos + 5;
    bites.resize(newSize);
    this->length = newSize;

    const uint8_t a = static_cast<uint8_t>(x >> 24);
    const uint8_t b = static_cast<uint8_t>(x >> 16);
    const uint8_t c = static_cast<uint8_t>(x >> 8);
    const uint8_t d = static_cast<uint8_t>(x);

    bites[pos++] = static_cast<uint8_t>(prop);
    bites[pos++] = a;
    bites[pos++] = b;
    bites[pos++] = c;
    bites[pos] = d;
}

void Mqtt5PropertyBuilder::writeUint16(Mqtt5Properties prop, const uint16_t x)
{
    size_t pos = bites.size();
    const size_t newSize = pos + 3;
    bites.resize(newSize);
    this->length = newSize;

    const uint8_t a = static_cast<uint8_t>(x >> 8);
    const uint8_t b = static_cast<uint8_t>(x);

    bites[pos++] = static_cast<uint8_t>(prop);
    bites[pos++] = a;
    bites[pos] = b;
}

void Mqtt5PropertyBuilder::writeUint8(Mqtt5Properties prop, const uint8_t x)
{
    size_t pos = bites.size();
    const size_t newSize = pos + 2;
    bites.resize(newSize);
    this->length = newSize;

    bites[pos++] = static_cast<uint8_t>(prop);
    bites[pos] = x;
}

void Mqtt5PropertyBuilder::writeStr(Mqtt5Properties prop, const std::string &str)
{
    if (str.length() > 65535)
        throw ProtocolError("String too long.");

    const uint16_t strlen = str.length();

    size_t pos = bites.size();
    const size_t newSize = pos + strlen + 2;
    bites.resize(newSize);
    this->length = newSize;

    const uint8_t a = static_cast<uint8_t>(strlen >> 8);
    const uint8_t b = static_cast<uint8_t>(strlen);

    bites[pos++] = static_cast<uint8_t>(prop);
    bites[pos++] = a;
    bites[pos++] = b;

    std::memcpy(&bites[pos], str.c_str(), strlen);
}

