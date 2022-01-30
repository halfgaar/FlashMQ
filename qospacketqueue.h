#ifndef QOSPACKETQUEUE_H
#define QOSPACKETQUEUE_H

#include "list"

#include "forward_declarations.h"
#include "types.h"

class QoSPacketQueue
{
    std::list<std::shared_ptr<MqttPacket>> queue; // Using list because it's easiest to maintain order [MQTT-4.6.0-6]
    ssize_t qosQueueBytes = 0;

public:
    void erase(const uint16_t packet_id);
    size_t size() const;
    size_t getByteSize() const;
    std::shared_ptr<MqttPacket> queuePacket(const MqttPacket &p, uint16_t id, char new_max_qos);
    std::shared_ptr<MqttPacket> queuePacket(const Publish &pub, uint16_t id);

    std::list<std::shared_ptr<MqttPacket>>::const_iterator begin() const;
    std::list<std::shared_ptr<MqttPacket>>::const_iterator end() const;
};

#endif // QOSPACKETQUEUE_H
