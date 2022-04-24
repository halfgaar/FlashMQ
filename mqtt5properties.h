#ifndef MQTT5PROPERTIES_H
#define MQTT5PROPERTIES_H

#include <vector>

#include "types.h"
#include "variablebyteint.h"

class Mqtt5PropertyBuilder
{
    std::vector<char> genericBytes;
    std::vector<char> clientSpecificBytes; // only relevant for publishes
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> userProperties;
    VariableByteInt length;

    void writeUint32(Mqtt5Properties prop, const uint32_t x, std::vector<char> &target) const;
    void writeUint16(Mqtt5Properties prop, const uint16_t x);
    void writeUint16(Mqtt5Properties prop, const uint16_t x, std::vector<char> &target) const;
    void writeUint8(Mqtt5Properties prop, const uint8_t x);
    void writeStr(Mqtt5Properties prop, const std::string &str);
    void write2Str(Mqtt5Properties prop, const std::string &one, const std::string &two);
public:
    Mqtt5PropertyBuilder();

    size_t getLength();
    const VariableByteInt &getVarInt();
    const std::vector<char> &getGenericBytes() const;
    const std::vector<char> &getclientSpecificBytes() const;
    void clearClientSpecificBytes();
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> getUserProperties() const;

    void writeSessionExpiry(uint32_t val);
    void writeReceiveMax(uint16_t val);
    void writeRetainAvailable(uint8_t val);
    void writeMaxPacketSize(uint32_t val);
    void writeAssignedClientId(const std::string &clientid);
    void writeMaxTopicAliases(uint16_t val);
    void writeWildcardSubscriptionAvailable(uint8_t val);
    void writeSubscriptionIdentifiersAvailable(uint8_t val);
    void writeSharedSubscriptionAvailable(uint8_t val);
    void writeContentType(const std::string &format);
    void writePayloadFormatIndicator(uint8_t val);
    void writeMessageExpiryInterval(uint32_t val);
    void writeResponseTopic(const std::string &str);
    void writeUserProperty(std::string &&key, std::string &&value);
    void writeCorrelationData(const std::string &correlationData);
    void writeTopicAlias(const uint16_t id);
    void writeAuthenticationMethod(const std::string &method);
    void writeAuthenticationData(const std::string &data);
    void setNewUserProperties(const std::shared_ptr<std::vector<std::pair<std::string, std::string>>> &userProperties);
};

#endif // MQTT5PROPERTIES_H
