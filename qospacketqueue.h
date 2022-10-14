#ifndef QOSPACKETQUEUE_H
#define QOSPACKETQUEUE_H

#include "list"
#include "map"

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
    QueuedPublish(const QueuedPublish &other) = delete;

    std::shared_ptr<QueuedPublish> prev;
    std::shared_ptr<QueuedPublish> next;

    size_t getApproximateMemoryFootprint() const;
    uint16_t getPacketId() const;
    Publish &getPublish();
};

class QoSPublishQueue
{
    std::shared_ptr<QueuedPublish> head;
    std::shared_ptr<QueuedPublish> tail;

    std::unordered_map<uint16_t, std::shared_ptr<QueuedPublish>> queue;
    std::map<std::chrono::time_point<std::chrono::steady_clock>, uint16_t> queueExpirations;
    std::chrono::time_point<std::chrono::steady_clock> nextExpireAt = std::chrono::time_point<std::chrono::steady_clock>::max();

    ssize_t qosQueueBytes = 0;

    void addToExpirationQueue(std::shared_ptr<QueuedPublish> &qp);
    void eraseFromMapAndRelinkList(std::unordered_map<uint16_t, std::shared_ptr<QueuedPublish>>::iterator pos);
    void addToHeadOfLinkedList(std::shared_ptr<QueuedPublish> &qp);

public:
    bool erase(const uint16_t packet_id);
    size_t size() const;
    size_t getByteSize() const;
    void queuePublish(PublishCopyFactory &copyFactory, uint16_t id, char new_max_qos);
    void queuePublish(Publish &&pub, uint16_t id);
    int clearExpiredMessages();
    std::shared_ptr<QueuedPublish> next();

};

#endif // QOSPACKETQUEUE_H
