#ifndef PUBLISHCOPYFACTORY_H
#define PUBLISHCOPYFACTORY_H

#include <vector>

#include "forward_declarations.h"
#include "types.h"
#include "unordered_map"

/**
 * @brief The PublishCopyFactory class is for managing copies of an incoming publish, including sometimes not making copies at all.
 *
 * The idea is that certain incoming packets can just be written to the receiving client as-is, without constructing a new one. We do have to change the bytes
 * where the QoS is stored, so we keep track of the original.
 *
 * Ownership info: object of this type are never copied or transferred, so the internal pointers come from (near) the same scope these objects
 * are created from.
 */
class PublishCopyFactory
{
    MqttPacket *packet = nullptr;
    Publish *publish = nullptr;
    std::unique_ptr<MqttPacket> oneShotPacket;
    const char orgQos;
    std::unordered_map<uint8_t, std::unique_ptr<MqttPacket>> constructedPacketCache;
public:
    PublishCopyFactory(MqttPacket *packet);
    PublishCopyFactory(Publish *publish);
    PublishCopyFactory(const PublishCopyFactory &other) = delete;
    PublishCopyFactory(PublishCopyFactory &&other) = delete;

    MqttPacket *getOptimumPacket(const char max_qos, const ProtocolVersion protocolVersion);
    char getEffectiveQos(char max_qos) const;
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics();
    bool getRetain() const;
    Publish getNewPublish() const;
    std::shared_ptr<Client> getSender();
};

#endif // PUBLISHCOPYFACTORY_H
