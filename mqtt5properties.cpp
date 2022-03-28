#include "mqtt5properties.h"

#include "cstring"
#include "vector"
#include "cassert"

#include "exceptions.h"

Mqtt5PropertyBuilder::Mqtt5PropertyBuilder()
{
    genericBytes.reserve(128);
    clientSpecificBytes.reserve(128);
}

size_t Mqtt5PropertyBuilder::getLength()
{
    length = genericBytes.size() + clientSpecificBytes.size();
    return length.getLen() + genericBytes.size() + clientSpecificBytes.size();
}

const VariableByteInt &Mqtt5PropertyBuilder::getVarInt()
{
    length = genericBytes.size() + clientSpecificBytes.size();
    return length;
}

const std::vector<char> &Mqtt5PropertyBuilder::getGenericBytes() const
{
    return genericBytes;
}

const std::vector<char> &Mqtt5PropertyBuilder::getclientSpecificBytes() const
{
    return clientSpecificBytes;
}

void Mqtt5PropertyBuilder::clearClientSpecificBytes()
{
    clientSpecificBytes.clear();
}

std::shared_ptr<std::vector<std::pair<std::string, std::string>>> Mqtt5PropertyBuilder::getUserProperties() const
{
    return this->userProperties;
}

void Mqtt5PropertyBuilder::writeSessionExpiry(uint32_t val)
{
    writeUint32(Mqtt5Properties::SessionExpiryInterval, val, genericBytes);
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
    writeUint32(Mqtt5Properties::MaximumPacketSize, val, genericBytes);
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
    writeUint32(Mqtt5Properties::MessageExpiryInterval, val, clientSpecificBytes);
}

void Mqtt5PropertyBuilder::writeResponseTopic(const std::string &str)
{
    writeStr(Mqtt5Properties::ResponseTopic, str);
}

void Mqtt5PropertyBuilder::writeUserProperty(std::string &&key, std::string &&value)
{
    write2Str(Mqtt5Properties::UserProperty, key, value);

    if (!this->userProperties)
        this->userProperties = std::make_shared<std::vector<std::pair<std::string, std::string>>>();

    std::pair<std::string, std::string> pair(std::move(key), std::move(value));
    this->userProperties->push_back(std::move(pair));
}

void Mqtt5PropertyBuilder::writeCorrelationData(const std::string &correlationData)
{
    writeStr(Mqtt5Properties::CorrelationData, correlationData);
}

void Mqtt5PropertyBuilder::writeTopicAlias(const uint16_t id)
{
    writeUint16(Mqtt5Properties::TopicAlias, id, clientSpecificBytes);
}

void Mqtt5PropertyBuilder::setNewUserProperties(const std::shared_ptr<std::vector<std::pair<std::string, std::string>>> &userProperties)
{
    assert(!this->userProperties);
    assert(this->genericBytes.empty());
    assert(this->clientSpecificBytes.empty());

    this->userProperties = userProperties;
}

void Mqtt5PropertyBuilder::writeUint32(Mqtt5Properties prop, const uint32_t x, std::vector<char> &target) const
{
    size_t pos = target.size();
    const size_t newSize = pos + 5;
    target.resize(newSize);

    const uint8_t a = static_cast<uint8_t>(x >> 24);
    const uint8_t b = static_cast<uint8_t>(x >> 16);
    const uint8_t c = static_cast<uint8_t>(x >> 8);
    const uint8_t d = static_cast<uint8_t>(x);

    target[pos++] = static_cast<uint8_t>(prop);
    target[pos++] = a;
    target[pos++] = b;
    target[pos++] = c;
    target[pos] = d;
}

void Mqtt5PropertyBuilder::writeUint16(Mqtt5Properties prop, const uint16_t x)
{
    writeUint16(prop, x, this->genericBytes);
}

void Mqtt5PropertyBuilder::writeUint16(Mqtt5Properties prop, const uint16_t x, std::vector<char> &target) const
{
    size_t pos = target.size();
    const size_t newSize = pos + 3;
    target.resize(newSize);

    const uint8_t a = static_cast<uint8_t>(x >> 8);
    const uint8_t b = static_cast<uint8_t>(x);

    target[pos++] = static_cast<uint8_t>(prop);
    target[pos++] = a;
    target[pos] = b;
}

void Mqtt5PropertyBuilder::writeUint8(Mqtt5Properties prop, const uint8_t x)
{
    size_t pos = genericBytes.size();
    const size_t newSize = pos + 2;
    genericBytes.resize(newSize);

    genericBytes[pos++] = static_cast<uint8_t>(prop);
    genericBytes[pos] = x;
}

void Mqtt5PropertyBuilder::writeStr(Mqtt5Properties prop, const std::string &str)
{
    if (str.length() > 65535)
        throw ProtocolError("String too long.");

    const uint16_t strlen = str.length();

    size_t pos = genericBytes.size();
    const size_t newSize = pos + strlen + 3;
    genericBytes.resize(newSize);

    const uint8_t a = static_cast<uint8_t>(strlen >> 8);
    const uint8_t b = static_cast<uint8_t>(strlen);

    genericBytes[pos++] = static_cast<uint8_t>(prop);
    genericBytes[pos++] = a;
    genericBytes[pos++] = b;

    std::memcpy(&genericBytes[pos], str.c_str(), strlen);
}

void Mqtt5PropertyBuilder::write2Str(Mqtt5Properties prop, const std::string &one, const std::string &two)
{
    size_t pos = genericBytes.size();
    const size_t newSize = pos + one.length() + two.length() + 5;
    genericBytes.resize(newSize);

    genericBytes[pos++] = static_cast<uint8_t>(prop);

    std::vector<const std::string*> strings;
    strings.push_back(&one);
    strings.push_back(&two);

    for (const std::string *str : strings)
    {
        if (str->length() > 0xFFFF)
            throw ProtocolError("String too long.");

        const uint16_t strlen = str->length();

        const uint8_t a = static_cast<uint8_t>(strlen >> 8);
        const uint8_t b = static_cast<uint8_t>(strlen);

        genericBytes[pos++] = a;
        genericBytes[pos++] = b;

        std::memcpy(&genericBytes[pos], str->c_str(), strlen);
        pos += strlen;
    }
}

