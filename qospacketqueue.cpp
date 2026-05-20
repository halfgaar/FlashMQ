/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "qospacketqueue.h"

#include <cassert>

QueuedPublish::QueuedPublish(Publish &&publish, uint16_t packet_id, const std::optional<std::string> &topic_override) :
    publish(std::move(publish)),
    packet_id(packet_id),
    topic_override(topic_override)
{

}

uint16_t QueuedPublish::getPacketId() const
{
    return this->packet_id;
}

Publish &QueuedPublish::getPublish()
{
    return publish;
}

const std::optional<std::string> &QueuedPublish::getTopicOverride() const
{
    return topic_override;
}

size_t QueuedPublish::getApproximateMemoryFootprint() const
{
    // TODO: hmm, this is possibly very inaccurate with MQTT5 packets.

    return publish.topic.length() + publish.payload.length();
}


void QoSPublishQueue::addToExpirationQueue(std::shared_ptr<QueuedPublish> &qp)
{
    if (!qp)
        return;

    Publish &pub = qp->getPublish();

    if (!pub.expireInfo)
        return;

    const auto when = std::chrono::time_point_cast<std::chrono::seconds>(pub.expiresAt().value());
    this->queueExpirations.emplace(when, qp);
}

bool QoSPublishQueue::erase(const uint16_t packet_id)
{
    bool result = false;
    auto pos = this->queue.find(packet_id);

    if (pos != this->queue.end())
    {
        std::shared_ptr<QueuedPublish> &qp = pos->second;
        subtractQoSBytes(qp.get());
        result = true;
        this->eraseFromMapAndRelinkList(pos);
    }

    return  result;
}

/**
 * @brief QoSPublishQueue::eraseFromMapAndRelinkList Removes a QueuedPublish from the unordered_map and relink previous andnext together.
 * @param pos
 *
 * This is an internal helper, and therefore doesn't do anything with qosQueueBytes.
 */
void QoSPublishQueue::eraseFromMapAndRelinkList(std::unordered_map<uint16_t, std::shared_ptr<QueuedPublish>>::iterator pos)
{
    std::shared_ptr<QueuedPublish> &qp = pos->second;

    auto _prev = qp->prev.lock();

    if (_prev)
        _prev->next = qp->next;

    if (this->head == qp)
        this->head = qp->prev.lock();

    auto _next = qp->next.lock();

    if (_next)
        _next->prev = qp->prev;

    if (this->tail == qp)
        this->tail = qp->next.lock();

    this->queue.erase(pos);
}

const std::shared_ptr<QueuedPublish> &QoSPublishQueue::getTail() const
{
    return tail;
}

#ifdef TESTING
// This can't be used in the normal program because we don't have access to the used IDs here.
std::shared_ptr<QueuedPublish> QoSPublishQueue::popNext()
{
    std::shared_ptr<QueuedPublish> result = this->tail;

    if (result)
        erase(result->getPacketId());

    return result;
}
#endif

size_t QoSPublishQueue::size() const
{
    return queue.size();
}

size_t QoSPublishQueue::getByteSize() const
{
    return qosQueueBytes;
}

void QoSPublishQueue::addToHeadOfLinkedList(std::shared_ptr<QueuedPublish> &qp)
{
    qp->prev = this->head;
    if (this->head)
        this->head->next = qp;
    this->head = qp;

    if (!this->tail)
        this->tail = qp;
}

void QoSPublishQueue::recalculateQosQueueBytes()
{
    size_t new_value{};

    for (const auto &x : queue)
    {
        if (!x.second)
            continue;
        new_value += x.second->getApproximateMemoryFootprint();
    }

    qosQueueBytes = { static_cast<ssize_t>(std::clamp<size_t>(new_value, 0, std::numeric_limits<ssize_t>::max())) };
}

void QoSPublishQueue::subtractQoSBytes(const QueuedPublish *p)
{
    if (!p)
        return;

    qosQueueBytes -= p->getApproximateMemoryFootprint();
    assert(qosQueueBytes >= 0);

    // Should not happen, but correcting a hypothetical bug is fine for this purpose.
    if (qosQueueBytes < 0)
        qosQueueBytes = 0;
}

/**
 * @brief QoSPublishQueue::queuePublish
 *
 * Note that it may seem a bit weird to queue messages with retain flags, because retained messages can only happen on
 * subscribe, which an offline client can't do. However, MQTT5 introduces 'retained as published', so it becomes valid. Bridge
 * mode uses this as well.
 */
void QoSPublishQueue::queuePublish(
    PublishCopyFactory &copyFactory, uint16_t id, uint8_t new_max_qos, bool retainAsPublished, const uint32_t subscriptionIdentifier,
    const std::optional<std::string> &topic_override)
{
    assert(new_max_qos > 0);
    assert(id > 0);

    Publish pub = copyFactory.getNewPublish(new_max_qos, retainAsPublished, subscriptionIdentifier);
    std::shared_ptr<QueuedPublish> qp = std::make_shared<QueuedPublish>(std::move(pub), id, topic_override);
    addToHeadOfLinkedList(qp);
    qosQueueBytes += qp->getApproximateMemoryFootprint();
    addToExpirationQueue(qp);
    queue[id] = std::move(qp);
}

/**
 * @brief QoSPublishQueue::queuePublish moves the publish into the queue.
 * @param pub
 * @param id
 * @param topic_override could/should theoretically also have been an rvalue ref, but that required maintaining
 * a various constructors of QueuedPublish with ref and rref arguments, which didn't seem worth it. So far, this
 * function is only used for loading from disk, so not the hot path.
 */
void QoSPublishQueue::queuePublish(Publish &&pub, uint16_t id, const std::optional<std::string> &topic_override)
{
    assert(id > 0);

    std::shared_ptr<QueuedPublish> qp = std::make_shared<QueuedPublish>(std::move(pub), id, topic_override);
    addToHeadOfLinkedList(qp);
    qosQueueBytes += qp->getApproximateMemoryFootprint();
    addToExpirationQueue(qp);
    queue[id] = std::move(qp);
}

std::vector<uint16_t> QoSPublishQueue::clearExpiredMessages()
{
    if (this->queueExpirations.empty())
        return {};

    if (this->queueExpirations.begin()->first > std::chrono::steady_clock::now())
        return {};

    std::vector<uint16_t> removed_ids;

    for (auto _ = queueExpirations.begin(); _ != queueExpirations.end();)
    {
        auto cur_it = _++;

        if (cur_it->first > std::chrono::steady_clock::now())
            break;

        const std::shared_ptr<QueuedPublish> qp = cur_it->second.lock();
        queueExpirations.erase(cur_it);

        if (!qp)
            continue;

        const auto qpos = this->queue.find(qp->getPacketId());

        if (qpos != this->queue.end() && qpos->second == qp)
        {
            removed_ids.push_back(qp->getPacketId());
            subtractQoSBytes(qp.get());
            this->eraseFromMapAndRelinkList(qpos);
        }
    }

    return removed_ids;
}


