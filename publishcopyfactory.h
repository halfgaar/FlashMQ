#ifndef PUBLISHCOPYFACTORY_H
#define PUBLISHCOPYFACTORY_H

#include <vector>
#include <unordered_map>

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
    std::unique_ptr<MqttPacket> oneShotPacket;
    const uint8_t orgQos;
    std::unordered_map<uint8_t, std::unique_ptr<MqttPacket>> constructedPacketCache;
    size_t sharedSubscriptionHashKey;
public:
    PublishCopyFactory(MqttPacket *packet);
    PublishCopyFactory(Publish *publish);
    PublishCopyFactory(const PublishCopyFactory &other) = delete;
    PublishCopyFactory(PublishCopyFactory &&other) = delete;

    MqttPacket *getOptimumPacket(const uint8_t max_qos, const ProtocolVersion protocolVersion, uint16_t topic_alias, bool skip_topic);
    uint8_t getEffectiveQos(uint8_t max_qos) const;
    const std::string &getTopic() const;
    const std::vector<std::string> &getSubtopics();
    bool getRetain() const;
    Publish getNewPublish(uint8_t new_max_qos) const;
    std::shared_ptr<Client> getSender();
    const std::vector<std::pair<std::string, std::string>> *getUserProperties() const;
    void setSharedSubscriptionHashKey(size_t hash);
    size_t getSharedSubscriptionHashKey() const;

};

#endif // PUBLISHCOPYFACTORY_H
