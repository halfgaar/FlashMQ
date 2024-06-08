/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <cassert>
#include <stdexcept>

#include "publishcopyfactory.h"
#include "mqttpacket.h"

PublishCopyFactory::PublishCopyFactory(MqttPacket *packet) :
    packet(packet),
    orgQos(packet->getQos()),
    orgRetain(packet->getRetain())
{

}

PublishCopyFactory::PublishCopyFactory(Publish *publish) :
    publish(publish),
    orgQos(publish->qos),
    orgRetain(publish->retain)
{

}

MqttPacket *PublishCopyFactory::getOptimumPacket(const uint8_t max_qos, const ProtocolVersion protocolVersion, uint16_t topic_alias, bool skip_topic)
{
    const uint8_t actualQos = getEffectiveQos(max_qos);

    if (packet)
    {
        // The incoming topic alias is not relevant after initial conversion and it should not propagate.
        assert(packet->getPublishData().topicAlias == 0);

        if (protocolVersion >= ProtocolVersion::Mqtt5 && (packet->containsClientSpecificProperties() || topic_alias > 0))
        {
            this->oneShotPacket = std::make_unique<MqttPacket>(protocolVersion, packet->getPublishData(), actualQos, topic_alias, skip_topic);
            return this->oneShotPacket.get();
        }

        if (packet->getProtocolVersion() == protocolVersion && static_cast<bool>(orgQos) == static_cast<bool>(actualQos) && !packet->isAlteredByPlugin())
        {
            return packet;
        }

        const int cache_key = (static_cast<uint8_t>(protocolVersion) * 10) + actualQos;
        std::unique_ptr<MqttPacket> &cachedPack = constructedPacketCache[cache_key];

        if (!cachedPack)
        {
            cachedPack = std::make_unique<MqttPacket>(protocolVersion, packet->getPublishData(), actualQos, 0, false);
        }

        return cachedPack.get();
    }

    // Getting an instance of a Publish object happens at least on retained messages, will messages and SYS topics. It's low traffic, anyway.
    assert(publish);

    // The incoming topic alias is not relevant after initial conversion and it should not propagate.
    assert(publish->topicAlias == 0);

    this->oneShotPacket = std::make_unique<MqttPacket>(protocolVersion, *publish, actualQos, topic_alias, skip_topic);
    return this->oneShotPacket.get();
}

uint8_t PublishCopyFactory::getEffectiveQos(uint8_t max_qos) const
{
    const uint8_t effectiveQos = std::min<uint8_t>(orgQos, max_qos);
    return effectiveQos;
}

bool PublishCopyFactory::getEffectiveRetain(bool retainAsPublished) const
{
    return orgRetain && retainAsPublished;
}

const std::string &PublishCopyFactory::getTopic() const
{
    if (packet)
        return packet->getTopic();
    assert(publish);
    return publish->topic;
}

const std::vector<std::string> &PublishCopyFactory::getSubtopics()
{
    if (packet)
    {
        return packet->getSubtopics();
    }
    else if (publish)
    {
        return publish->getSubtopics();
    }

    throw std::runtime_error("Bug in &PublishCopyFactory::getSubtopics()");
}

std::string_view PublishCopyFactory::getPayload() const
{
    if (packet)
        return packet->getPayloadView();
    assert(publish);
    return publish->payload;
}

bool PublishCopyFactory::getRetain() const
{
    // Keeping this here as reminder that it should not be implemented.
    assert(false);
    return false;
}

/**
 * @brief PublishCopyFactory::getNewPublish gets a new publish object from an existing packet or publish.
 * @param new_max_qos
 * @return
 *
 * It being a public function, the idea is that it's only needed for creating publish objects for storing QoS messages for off-line
 * clients. For on-line clients, you're always making a packet (with getOptimumPacket()).
 */
Publish PublishCopyFactory::getNewPublish(uint8_t new_max_qos, bool retainAsPublished) const
{
    // (At time of writing) we only need to construct new publishes for QoS (because we're storing QoS publishes for offline clients). If
    // you're doing it elsewhere, it's a bug.
    assert(orgQos > 0);
    assert(new_max_qos > 0);

    const uint8_t actualQos = getEffectiveQos(new_max_qos);

    if (packet)
    {
        assert(packet->getQos() > 0);

        Publish p(packet->getPublishData());
        p.qos = actualQos;
        p.retain = getEffectiveRetain(retainAsPublished);
        return p;
    }

    assert(publish->qos > 0); // Same check as above, but then for Publish objects.

    Publish p(*publish);
    p.qos = actualQos;
    p.retain = getEffectiveRetain(retainAsPublished);
    return p;
}

std::shared_ptr<Client> PublishCopyFactory::getSender()
{
    if (packet)
        return packet->getSender();
    return std::shared_ptr<Client>(0);
}

const std::vector<std::pair<std::string, std::string> > *PublishCopyFactory::getUserProperties() const
{
    if (packet)
    {
        return packet->getUserProperties();
    }

    assert(publish);

    return publish->getUserProperties();
}

void PublishCopyFactory::setSharedSubscriptionHashKey(size_t hash)
{
    this->sharedSubscriptionHashKey = hash;
}

