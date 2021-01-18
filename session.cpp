#include "cassert"

#include "session.h"
#include "client.h"

Session::Session()
{

}

bool Session::clientDisconnected() const
{
    return client.expired();
}

std::shared_ptr<Client> Session::makeSharedClient() const
{
    return client.lock();
}

void Session::assignActiveConnection(std::shared_ptr<Client> &client)
{
    this->client = client;
    this->client_id = client->getClientId();
}

void Session::writePacket(const MqttPacket &packet, char max_qos)
{
    const char qos = std::min<char>(packet.getQos(), max_qos);

    if (qos == 0)
    {
        if (!clientDisconnected())
        {
            Client_p c = makeSharedClient();
            c->writeMqttPacketAndBlameThisClient(packet);
        }
    }
    else if (qos == 1)
    {
        std::shared_ptr<MqttPacket> copyPacket = packet.getCopy();
        std::unique_lock<std::mutex> locker(qosQueueMutex);
        if (qosPacketQueue.size() >= MAX_QOS_MSG_PENDING_PER_CLIENT || (qosQueueBytes >= MAX_QOS_BYTES_PENDING_PER_CLIENT && qosPacketQueue.size() > 0))
        {
            logger->logf(LOG_WARNING, "Dropping QoS message for client '%s', because its QoS buffers were full.", client_id.c_str());
            return;
        }
        const uint16_t pid = nextPacketId++;
        copyPacket->setPacketId(pid);
        QueuedQosPacket p;
        p.packet = copyPacket;
        p.id = pid;
        qosPacketQueue.push_back(p);
        qosQueueBytes += copyPacket->getTotalMemoryFootprint();
        locker.unlock();

        if (!clientDisconnected())
        {
            Client_p c = makeSharedClient();
            c->writeMqttPacketAndBlameThisClient(*copyPacket.get());
            copyPacket->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
        }
    }
}

// Normatively, this while loop will break on the first element, because all messages are sent out in order and
// should be acked in order.
void Session::clearQosMessage(uint16_t packet_id)
{
    std::lock_guard<std::mutex> locker(qosQueueMutex);

    auto it = qosPacketQueue.begin();
    auto end = qosPacketQueue.end();
    while (it != end)
    {
        QueuedQosPacket &p = *it;
        if (p.id == packet_id)
        {
            size_t mem = p.packet->getTotalMemoryFootprint();
            qosQueueBytes -= mem;
            assert(qosQueueBytes >= 0);
            if (qosQueueBytes < 0) // Should not happen, but correcting a hypothetical bug is fine for this purpose.
                qosQueueBytes = 0;

            qosPacketQueue.erase(it);

            break;
        }

        it++;
    }
}

// [MQTT-4.4.0-1]: "When a Client reconnects with CleanSession set to 0, both the Client and Server MUST re-send any
// unacknowledged PUBLISH Packets (where QoS > 0) and PUBREL Packets using their original Packet Identifiers. This
// is the only circumstance where a Client or Server is REQUIRED to redeliver messages."
//
// There is a bit of a hole there, I think. When we write out a packet to a receiver, it may decide to drop it, if its buffers
// are full, for instance. We are not required to (periodically) retry. TODO Perhaps I will implement that retry anyway.
void Session::sendPendingQosMessages()
{
    if (!clientDisconnected())
    {
        Client_p c = makeSharedClient();
        std::lock_guard<std::mutex> locker(qosQueueMutex);
        for (QueuedQosPacket &qosMessage : qosPacketQueue)
        {
            c->writeMqttPacketAndBlameThisClient(*qosMessage.packet.get());
            qosMessage.packet->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
        }
    }
}
