#ifndef QOSPACKETQUEUE_H
#define QOSPACKETQUEUE_H

#include "list"

#include "forward_declarations.h"
#include "types.h"
#include "publishcopyfactory.h"

/**
 * @brief The QueuedPublish class wraps the publish with a packet id.
 *
 * We don't want to store the packet id in the Publish object, because the packet id is determined/tracked per client/session.
 */
class QueuedPublish
{
    Publish publish;
    uint16_t packet_id = 0;
public:
    QueuedPublish(Publish &&publish, uint16_t packet_id);

    size_t getApproximateMemoryFootprint() const;
    uint16_t getPacketId() const;
    Publish &getPublish();
};

class QoSPublishQueue
{
    std::list<QueuedPublish> queue; // Using list because it's easiest to maintain order [MQTT-4.6.0-6]
    ssize_t qosQueueBytes = 0;

public:
    bool erase(const uint16_t packet_id);
    std::list<QueuedPublish>::iterator erase(std::list<QueuedPublish>::iterator pos);
    size_t size() const;
    size_t getByteSize() const;
    void queuePublish(PublishCopyFactory &copyFactory, uint16_t id, char new_max_qos);
    void queuePublish(Publish &&pub, uint16_t id);

    std::list<QueuedPublish>::iterator begin();
    std::list<QueuedPublish>::iterator end();
};

#endif // QOSPACKETQUEUE_H
