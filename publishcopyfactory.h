/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef PUBLISHCOPYFACTORY_H
#define PUBLISHCOPYFACTORY_H

#include <vector>
#include <unordered_map>
#include <optional>

#include "mqttpacket.h"
#include "forward_declarations.h"
#include "types.h"

/**
 * @brief The PublishCopyFactory class is for managing copies of an incoming publish, including sometimes not making copies at all.
 *
 * The idea is that certain incoming packets can just be written to the receiving client as-is, without constructing a new one. We do have to change the bytes
 * where the QoS is stored, so we keep track of the original QoS.
 *
 * Ownership info: object of this type are never copied or transferred, so the internal pointers come from (near) the same scope these objects
 * are created from.
 */
class PublishCopyFactory
{
    MqttPacket *packet = nullptr;
    Publish *publish = nullptr;
    std::optional<MqttPacket> oneShotPacket;
    const uint8_t orgQos;
    const bool orgRetain = false;
    std::unordered_map<uint8_t, std::optional<MqttPacket>> constructedPacketCache;
    size_t sharedSubscriptionHashKey;
public:
    PublishCopyFactory(MqttPacket *packet);
    PublishCopyFactory(Publish *publish);
    PublishCopyFactory(const PublishCopyFactory &other) = delete;
    PublishCopyFactory(PublishCopyFactory &&other) = delete;

    MqttPacket *getOptimumPacket(const uint8_t max_qos, const ProtocolVersion protocolVersion, uint16_t topic_alias, bool skip_topic, uint32_t subscriptionIdentifier);
    uint8_t getEffectiveQos(uint8_t max_qos) const;
    bool getEffectiveRetain(bool retainAsPublished) const;
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics();
    std::string_view getPayload() const;
    bool getRetain() const;
    Publish getNewPublish(uint8_t new_max_qos, bool retainAsPublished, uint32_t subscriptionIdentifier) const;
    const std::vector<std::pair<std::string, std::string>> *getUserProperties() const;
    const std::optional<std::string> &getCorrelationData() const;
    const std::optional<std::string> &getResponseTopic() const;
    void setSharedSubscriptionHashKey(size_t hash);
    size_t getSharedSubscriptionHashKey() const { return sharedSubscriptionHashKey; }
    static int getPublishLayoutCompareKey(ProtocolVersion pv, uint8_t qos);
};

#endif // PUBLISHCOPYFACTORY_H
