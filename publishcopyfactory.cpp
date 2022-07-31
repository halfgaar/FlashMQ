#include <cassert>

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

MqttPacket *PublishCopyFactory::getOptimumPacket(const char max_qos, const ProtocolVersion protocolVersion, uint16_t topic_alias, bool skip_topic)
{
    if (packet)
    {
        if (protocolVersion >= ProtocolVersion::Mqtt5 && (packet->containsClientSpecificProperties() || topic_alias > 0))
        {
            Publish newPublish(packet->getPublishData());
            newPublish.qos = max_qos;
            newPublish.topicAlias = topic_alias;
            newPublish.skipTopic = skip_topic;
            this->oneShotPacket = std::make_unique<MqttPacket>(protocolVersion, newPublish);
            return this->oneShotPacket.get();
        }

        if (packet->getProtocolVersion() == protocolVersion && orgQos == max_qos)
        {
            assert(orgQos == packet->getQos());
            return packet;
        }

        const int cache_key = (static_cast<uint8_t>(protocolVersion) * 10) + max_qos;
        std::unique_ptr<MqttPacket> &cachedPack = constructedPacketCache[cache_key];

        if (!cachedPack)
        {
            Publish newPublish(packet->getPublishData());
            newPublish.qos = max_qos;
            cachedPack = std::make_unique<MqttPacket>(protocolVersion, newPublish);
        }

        return cachedPack.get();
    }

    // Getting an instance of a Publish object happens at least on retained messages, will messages and SYS topics. It's low traffic, anyway.
    assert(publish);

    publish->qos = getEffectiveQos(max_qos);

    this->oneShotPacket = std::make_unique<MqttPacket>(protocolVersion, *publish);
    return this->oneShotPacket.get();
}

char PublishCopyFactory::getEffectiveQos(char max_qos) const
{
    const char effectiveQos = std::min<char>(orgQos, max_qos);
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

bool PublishCopyFactory::getRetain() const
{
    if (packet)
        return packet->getRetain();
    assert(publish);
    return publish->retain;
}

Publish PublishCopyFactory::getNewPublish() const
{
    if (packet)
    {
        assert(packet->getQos() > 0);
        assert(orgQos > 0); // We only need to construct new publishes for QoS. If you're doing it elsewhere, it's a bug.

        Publish p(packet->getPublishData());
        p.qos = orgQos;
        return p;
    }

    assert(publish->qos > 0); // Same check as above, but then for Publish objects.

    Publish p(*publish);
    p.qos = orgQos;
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
