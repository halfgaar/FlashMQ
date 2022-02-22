#include <cassert>

#include "publishcopyfactory.h"
#include "mqttpacket.h"

PublishCopyFactory::PublishCopyFactory(MqttPacket &packet) :
    packet(packet),
    orgQos(packet.getQos())
{

}

MqttPacket &PublishCopyFactory::getOptimumPacket(char max_qos)
{
    if (max_qos == 0 && max_qos < packet.getQos())
    {
        if (!downgradedQos0PacketCopy)
            downgradedQos0PacketCopy = packet.getCopy(max_qos);
        assert(downgradedQos0PacketCopy->getQos() == 0);
        return *downgradedQos0PacketCopy.get();
    }

    return packet;
}

char PublishCopyFactory::getEffectiveQos(char max_qos) const
{
    const char effectiveQos = std::min<char>(orgQos, max_qos);
    return effectiveQos;
}

const std::string &PublishCopyFactory::getTopic() const
{
    return packet.getTopic();
}

const std::vector<std::string> &PublishCopyFactory::getSubtopics() const
{
    return packet.getSubtopics();
}

bool PublishCopyFactory::getRetain() const
{
    return packet.getRetain();
}

Publish PublishCopyFactory::getPublish() const
{
    assert(packet.getQos() > 0);

    Publish p(packet.getTopic(), packet.getPayloadCopy(), packet.getQos());
    return p;
}
