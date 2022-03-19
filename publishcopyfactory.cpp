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

MqttPacket *PublishCopyFactory::getOptimumPacket(const char max_qos, const ProtocolVersion protocolVersion)
{
    if (packet)
    {
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
            newPublish.splitTopic = false;
            newPublish.qos = max_qos;
            if (protocolVersion >= ProtocolVersion::Mqtt5)
                newPublish.setClientSpecificProperties();
            cachedPack = std::make_unique<MqttPacket>(protocolVersion, newPublish);
        }

        return cachedPack.get();
    }

    // Getting a packet of a Publish object happens on will messages and SYS topics and maybe some others. It's low traffic, anyway.
    assert(publish);

    if (protocolVersion >= ProtocolVersion::Mqtt5)
        publish->setClientSpecificProperties();
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
        assert(!packet->getSubtopics().empty());
        return packet->getSubtopics();
    }
    else if (publish)
    {
        if (publish->subtopics.empty())
            splitTopic(publish->topic, publish->subtopics);
        return publish->subtopics;
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
    assert(packet->getQos() > 0);
    assert(orgQos > 0); // We only need to construct new publishes for QoS. If you're doing it elsewhere, it's a bug.

    if (packet)
    {
        Publish p(packet->getPublishData());
        p.qos = orgQos;
        return p;
    }

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
