#include "qospacketqueue.h"

#include "cassert"

#include "mqttpacket.h"

void QoSPacketQueue::erase(const uint16_t packet_id)
{
    auto it = queue.begin();
    auto end = queue.end();
    while (it != end)
    {
        std::shared_ptr<MqttPacket> &p = *it;
        if (p->getPacketId() == packet_id)
        {
            size_t mem = p->getTotalMemoryFootprint();
            qosQueueBytes -= mem;
            assert(qosQueueBytes >= 0);
            if (qosQueueBytes < 0) // Should not happen, but correcting a hypothetical bug is fine for this purpose.
                qosQueueBytes = 0;

            queue.erase(it);

            break;
        }

        it++;
    }
}

size_t QoSPacketQueue::size() const
{
    return queue.size();
}

size_t QoSPacketQueue::getByteSize() const
{
    return qosQueueBytes;
}

/**
 * @brief QoSPacketQueue::queuePacket makes a copy of the packet because it has state for the receiver in question.
 * @param p
 * @param id
 * @return the packet copy.
 */
std::shared_ptr<MqttPacket> QoSPacketQueue::queuePacket(const MqttPacket &p, uint16_t id, char new_max_qos)
{
    assert(p.getQos() > 0);

    std::shared_ptr<MqttPacket> copyPacket = p.getCopy(new_max_qos);
    copyPacket->setPacketId(id);
    queue.push_back(copyPacket);
    qosQueueBytes += copyPacket->getTotalMemoryFootprint();
    return copyPacket;
}

std::shared_ptr<MqttPacket> QoSPacketQueue::queuePacket(const Publish &pub, uint16_t id)
{
    assert(pub.qos > 0);

    std::shared_ptr<MqttPacket> copyPacket(new MqttPacket(pub));
    copyPacket->setPacketId(id);
    queue.push_back(copyPacket);
    qosQueueBytes += copyPacket->getTotalMemoryFootprint();
    return copyPacket;
}

std::list<std::shared_ptr<MqttPacket>>::const_iterator QoSPacketQueue::begin() const
{
    return queue.cbegin();
}

std::list<std::shared_ptr<MqttPacket>>::const_iterator QoSPacketQueue::end() const
{
    return queue.cend();
}
