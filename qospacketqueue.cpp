#include "qospacketqueue.h"

#include "cassert"

#include "mqttpacket.h"

QueuedPublish::QueuedPublish(Publish &&publish, uint16_t packet_id) :
    publish(std::move(publish)),
    packet_id(packet_id)
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

size_t QueuedPublish::getApproximateMemoryFootprint() const
{
    // TODO: hmm, this is possibly very inaccurate with MQTT5 packets.

    return publish.topic.length() + publish.payload.length();
}


void QoSPublishQueue::addToExpirationQueue(std::shared_ptr<QueuedPublish> &qp)
{
    Publish &pub = qp->getPublish();

    if (!pub.getHasExpireInfo())
        return;

    this->nextExpireAt = std::min(pub.expiresAt(), this->nextExpireAt);
    this->queueExpirations[pub.expiresAt()] = qp->getPacketId();
}

bool QoSPublishQueue::erase(const uint16_t packet_id)
{
    bool result = false;
    auto pos = this->queue.find(packet_id);

    if (pos != this->queue.end())
    {
        std::shared_ptr<QueuedPublish> &qp = pos->second;

        const size_t mem = qp->getApproximateMemoryFootprint();
        qosQueueBytes -= mem;
        assert(qosQueueBytes >= 0);
        if (qosQueueBytes < 0) // Should not happen, but correcting a hypothetical bug is fine for this purpose.
            qosQueueBytes = 0;

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

    if (qp->prev)
        qp->prev->next = qp->next;

    if (this->head == qp)
        this->head = qp->prev;

    if (qp->next)
        qp->next->prev = qp->prev;

    if (this->tail == qp)
        this->tail = qp->next;

    this->queue.erase(pos);
}

std::shared_ptr<QueuedPublish> QoSPublishQueue::next()
{
    std::shared_ptr<QueuedPublish> result = this->tail;
    if (this->tail)
        this->tail = this->tail->next;
    return result;
}

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

void QoSPublishQueue::queuePublish(PublishCopyFactory &copyFactory, uint16_t id, uint8_t new_max_qos)
{
    assert(new_max_qos > 0);
    assert(id > 0);

    Publish pub = copyFactory.getNewPublish(new_max_qos);
    std::shared_ptr<QueuedPublish> qp = std::make_shared<QueuedPublish>(std::move(pub), id);
    addToHeadOfLinkedList(qp);
    qosQueueBytes += qp->getApproximateMemoryFootprint();
    addToExpirationQueue(qp);
    queue[id] = std::move(qp);
}

void QoSPublishQueue::queuePublish(Publish &&pub, uint16_t id)
{
    assert(id > 0);

    std::shared_ptr<QueuedPublish> qp = std::make_shared<QueuedPublish>(std::move(pub), id);
    addToHeadOfLinkedList(qp);
    qosQueueBytes += qp->getApproximateMemoryFootprint();
    addToExpirationQueue(qp);
    queue[id] = std::move(qp);
}

int QoSPublishQueue::clearExpiredMessages()
{
    if (nextExpireAt > std::chrono::steady_clock::now() || this->queueExpirations.empty())
        return 0;

    this->nextExpireAt = std::chrono::time_point<std::chrono::steady_clock>::max();

    int removed = 0;

    auto it = queueExpirations.begin();
    auto end = queueExpirations.end();
    while (it != end)
    {
        auto cur_it = it;
        it++;

        const std::chrono::time_point<std::chrono::steady_clock> &when = cur_it->first;

        if (when > std::chrono::steady_clock::now())
        {
            this->nextExpireAt = when;
            break;
        }

        auto qpos = this->queue.find(cur_it->second);

        if (qpos != this->queue.end())
        {
            std::shared_ptr<QueuedPublish> &p = qpos->second;

            if (p->getPublish().hasExpired())
            {
                this->eraseFromMapAndRelinkList(qpos);

                this->queueExpirations.erase(cur_it);
                removed++;
            }
        }
    }

    return removed;
}


