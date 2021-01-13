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
}

void Session::writePacket(const MqttPacket &packet)
{
    const char qos = packet.getQos();

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
            logger->logf(LOG_WARNING, "Dropping QoS message for client 'TODO', because its QoS buffers were full.");
            return;
        }
        const uint16_t pid = nextPacketId++;
        copyPacket->setPacketId(pid);
        qosPacketQueue[pid] = copyPacket;
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

void Session::clearQosMessage(uint16_t packet_id)
{
    std::lock_guard<std::mutex> locker(qosQueueMutex);
    auto it = qosPacketQueue.find(packet_id);
    if (it != qosPacketQueue.end())
    {
        std::shared_ptr<MqttPacket> packet = it->second;
        qosPacketQueue.erase(it);
        qosQueueBytes -= packet->getTotalMemoryFootprint();
        assert(qosQueueBytes >= 0);
        if (qosQueueBytes < 0) // Should not happen, but correcting a hypothetical bug is fine for this purpose.
            qosQueueBytes = 0;
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
        for (auto &qosMessage : qosPacketQueue) // TODO: wrong: the order must be maintained. Combine the fix with that vector idea
        {
            c->writeMqttPacketAndBlameThisClient(*qosMessage.second.get());
            qosMessage.second->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
        }
    }
}
