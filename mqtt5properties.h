/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef MQTT5PROPERTIES_H
#define MQTT5PROPERTIES_H

#include <vector>

#include "types.h"
#include "variablebyteint.h"

class Mqtt5PropertyBuilder
{
    std::vector<char> bytes;
    VariableByteInt length;

    int userPropertyCount = 0;

    void writeUint32(Mqtt5Properties prop, const uint32_t x);
    void writeUint16(Mqtt5Properties prop, const uint16_t x);
    void writeUint8(Mqtt5Properties prop, const uint8_t x);
    void writeStr(Mqtt5Properties prop, const std::string &str);
    void write2Str(Mqtt5Properties prop, const std::string &one, const std::string &two);
public:
    Mqtt5PropertyBuilder();

    size_t getLength();
    const VariableByteInt &getVarInt();
    const std::vector<char> &getBytes() const;

    void writeServerKeepAlive(uint16_t val);
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
    void writeUserProperties(const std::vector<std::pair<std::string, std::string>> &properties);
    void writeUserProperty(const std::string &key, const std::string &value);
    void writeCorrelationData(const std::string &correlationData);
    void writeTopicAlias(const uint16_t id);
    void writeAuthenticationMethod(const std::string &method);
    void writeAuthenticationData(const std::string &data);
    void writeWillDelay(uint32_t delay);
};

#endif // MQTT5PROPERTIES_H
