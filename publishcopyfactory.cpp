#include <cassert>
#include <stdexcept>

#include "publishcopyfactory.h"
#include "mqttpacket.h"

PublishCopyFactory::PublishCopyFactory(MqttPacket *packet) :
    packet(packet),
    orgQos(packet->getQos())
{

}

PublishCopyFactory::PublishCopyFactory(Publish *publish) :
    publish(publish),
    orgQos(publish->qos)
{

}

MqttPacket *PublishCopyFactory::getOptimumPacket(const uint8_t max_qos, const ProtocolVersion protocolVersion, uint16_t topic_alias, bool skip_topic)
{
    const uint8_t actualQos = getEffectiveQos(max_qos);

    if (packet)
    {
        if (protocolVersion >= ProtocolVersion::Mqtt5 && (packet->containsClientSpecificProperties() || topic_alias > 0))
        {
            Publish newPublish(packet->getPublishData());
            newPublish.qos = actualQos;
            newPublish.topicAlias = topic_alias;
            newPublish.skipTopic = skip_topic;
            this->oneShotPacket = std::make_unique<MqttPacket>(protocolVersion, newPublish);
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
            Publish newPublish(packet->getPublishData());
            newPublish.qos = actualQos;
            cachedPack = std::make_unique<MqttPacket>(protocolVersion, newPublish);
        }

        return cachedPack.get();
    }

    // Getting an instance of a Publish object happens at least on retained messages, will messages and SYS topics. It's low traffic, anyway.
    assert(publish);

    publish->qos = actualQos;
    publish->topicAlias = topic_alias;
    publish->skipTopic = skip_topic;

    this->oneShotPacket = std::make_unique<MqttPacket>(protocolVersion, *publish);
    return this->oneShotPacket.get();
}

uint8_t PublishCopyFactory::getEffectiveQos(uint8_t max_qos) const
{
    const uint8_t effectiveQos = std::min<uint8_t>(orgQos, max_qos);
    return effectiveQos;
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
    if (packet)
        return packet->getRetain();
    assert(publish);
    return publish->retain;
}

/**
 * @brief PublishCopyFactory::getNewPublish gets a new publish object from an existing packet or publish.
 * @param new_max_qos
 * @return
 *
 * It being a public function, the idea is that it's only needed for creating publish objects for storing QoS messages for off-line
 * clients. For on-line clients, you're always making a packet (with getOptimumPacket()).
 */
Publish PublishCopyFactory::getNewPublish(uint8_t new_max_qos) const
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
        return p;
    }

    assert(publish->qos > 0); // Same check as above, but then for Publish objects.

    Publish p(*publish);
    p.qos = actualQos;
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

