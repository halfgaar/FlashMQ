#ifndef PUBLISHCOPYFACTORY_H
#define PUBLISHCOPYFACTORY_H

#include <vector>

#include "forward_declarations.h"
#include "types.h"

/**
 * @brief The PublishCopyFactory class is for managing copies of an incoming publish, including sometimes not making copies at all.
 *
 * The idea is that certain incoming packets can just be written to the receiving client as-is, without constructing a new one. We do have to change the bytes
 * where the QoS is stored, so we keep track of the original.
 */
class PublishCopyFactory
{
    MqttPacket &packet;
    const char orgQos;
    std::shared_ptr<MqttPacket> downgradedQos0PacketCopy;

    // TODO: constructed mqtt3 packet and mqtt5 packet
public:
    PublishCopyFactory(MqttPacket &packet);
    PublishCopyFactory(const PublishCopyFactory &other) = delete;
    PublishCopyFactory(PublishCopyFactory &&other) = delete;

    MqttPacket &getOptimumPacket(char max_qos);
    char getEffectiveQos(char max_qos) const;
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics() const;
    bool getRetain() const;
    Publish getPublish() const;
};

#endif // PUBLISHCOPYFACTORY_H
