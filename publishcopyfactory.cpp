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

MqttPacket *PublishCopyFactory::getOptimumPacket(char max_qos, ProtocolVersion protocolVersion)
{
    // TODO: cache idea: a set of constructed packets per branch in this code, witn an int identifier.

    if (packet)
    {
        // TODO: some flag on packet 'containsClientSpecificStuff'
        if (max_qos == 0 && max_qos < packet->getQos() && packet->getProtocolVersion() == protocolVersion && protocolVersion <= ProtocolVersion::Mqtt311)
        {
            // TODO: getCopy now also makes packets with Publish objects. I think I only want to do that here.

            if (!downgradedQos0PacketCopy)
                downgradedQos0PacketCopy = packet->getCopy(max_qos); // TODO: getCopy perhaps std::unique_ptr
            assert(downgradedQos0PacketCopy->getQos() == 0);
            return downgradedQos0PacketCopy.get();
        }
        else if (packet->getProtocolVersion() >= ProtocolVersion::Mqtt5 || protocolVersion >= ProtocolVersion::Mqtt5)
        {
            Publish *pub = packet->getPublish();
            pub->setClientSpecificProperties(); // TODO
            this->oneShotPacket = std::make_unique<MqttPacket>(protocolVersion, *pub);
            return this->oneShotPacket.get();
        }

        return packet;
    }

    assert(publish);

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
    assert(packet->getQos() > 0); // We only need to construct new publishes for QoS. If you're doing it elsewhere, it's a bug.

    if (packet)
    {
        Publish p(*packet->getPublish());
        return p;
    }

    Publish p(*publish);
    return p;
}

std::shared_ptr<Client> PublishCopyFactory::getSender()
{
    if (packet)
        return packet->getSender();
    return std::shared_ptr<Client>(0);
}
