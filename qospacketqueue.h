/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef QOSPACKETQUEUE_H
#define QOSPACKETQUEUE_H

#include <list>
#include <map>

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

    // We store this separately because because we need to retain the original publish path for ACL checking upon resending.
    std::optional<std::string> topic_override;
public:
    QueuedPublish(Publish &&publish, uint16_t packet_id, const std::optional<std::string> &topic_override);
    QueuedPublish(const QueuedPublish &other) = delete;

    std::shared_ptr<QueuedPublish> prev;
    std::shared_ptr<QueuedPublish> next;

    size_t getApproximateMemoryFootprint() const;
    uint16_t getPacketId() const;
    Publish &getPublish();
    const std::optional<std::string> &getTopicOverride() const;
};

class QoSPublishQueue
{
    std::shared_ptr<QueuedPublish> head;
    std::shared_ptr<QueuedPublish> tail;

    std::unordered_map<uint16_t, std::shared_ptr<QueuedPublish>> queue;
    std::map<std::chrono::time_point<std::chrono::steady_clock>, uint16_t> queueExpirations;

    ssize_t qosQueueBytes = 0;

    void addToExpirationQueue(std::shared_ptr<QueuedPublish> &qp);
    void eraseFromMapAndRelinkList(std::unordered_map<uint16_t, std::shared_ptr<QueuedPublish>>::iterator pos);
    void addToHeadOfLinkedList(std::shared_ptr<QueuedPublish> &qp);

public:
    QoSPublishQueue() = default;

    // We make this uncopyable because of the linked list QueuedPublish objects, making a deep-copy difficult.
    QoSPublishQueue(const QoSPublishQueue &other) = delete;
    QoSPublishQueue &operator=(const QoSPublishQueue &other) = delete;

    QoSPublishQueue(QoSPublishQueue &&other) = default;
    QoSPublishQueue &operator=(QoSPublishQueue &&other) = default;

    bool erase(const uint16_t packet_id);
    size_t size() const;
    size_t getByteSize() const;
    void queuePublish(
        PublishCopyFactory &copyFactory, uint16_t id, uint8_t new_max_qos, bool retainAsPublished, const uint32_t subscriptionIdentifier,
        const std::optional<std::string> &topic_override);
    void queuePublish(Publish &&pub, uint16_t id, const std::optional<std::string> &topic_override);
    int clearExpiredMessages();
    const std::shared_ptr<QueuedPublish> &getTail() const;
    std::shared_ptr<QueuedPublish> popNext();

};

#endif // QOSPACKETQUEUE_H
