#ifndef MQTT5PROPERTIES_H
#define MQTT5PROPERTIES_H

#include <vector>

#include "types.h"
#include "variablebyteint.h"

class Mqtt5PropertyBuilder
{
    std::vector<char> bites;
    VariableByteInt length;

    void writeUint32(Mqtt5Properties prop, const uint32_t x);
    void writeUint16(Mqtt5Properties prop, const uint16_t x);
    void writeUint8(Mqtt5Properties prop, const uint8_t x);
    void writeStr(Mqtt5Properties prop, const std::string &str);
public:
    Mqtt5PropertyBuilder();

    size_t getLength() const;
    const VariableByteInt &getVarInt() const;
    const std::vector<char> &getBites() const;

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
};

#endif // MQTT5PROPERTIES_H
