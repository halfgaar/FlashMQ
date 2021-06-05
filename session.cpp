/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#include "cassert"

#include "session.h"
#include "client.h"

Session::Session()
{

}

Session::~Session()
{
    logger->logf(LOG_DEBUG, "Session %s is being destroyed.", getClientId().c_str());
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
    this->username = client->getUsername();
    this->thread = client->getThreadData();
}

void Session::writePacket(const MqttPacket &packet, char max_qos, uint64_t &count)
{
    assert(max_qos <= 2);

    if (thread->authentication.aclCheck(client_id, username, packet.getTopic(), *packet.getSubtopics(), AclAccess::read) == AuthResult::success)
    {
        const char qos = std::min<char>(packet.getQos(), max_qos);

        if (qos == 0)
        {
            if (!clientDisconnected())
            {
                std::shared_ptr<Client> c = makeSharedClient();
                c->writeMqttPacketAndBlameThisClient(packet, qos);
                count++;
            }
        }
        else if (qos > 0)
        {
            std::shared_ptr<MqttPacket> copyPacket = packet.getCopy();
            std::unique_lock<std::mutex> locker(qosQueueMutex);

            const size_t totalQosPacketsInTransit = qosPacketQueue.size() + incomingQoS2MessageIds.size() + outgoingQoS2MessageIds.size();
            if (totalQosPacketsInTransit >= MAX_QOS_MSG_PENDING_PER_CLIENT || (qosQueueBytes >= MAX_QOS_BYTES_PENDING_PER_CLIENT && qosPacketQueue.size() > 0))
            {
                logger->logf(LOG_WARNING, "Dropping QoS message for client '%s', because its QoS buffers were full.", client_id.c_str());
                return;
            }
            nextPacketId++;
            if (nextPacketId == 0)
                nextPacketId++;

            const uint16_t pid = nextPacketId;
            copyPacket->setPacketId(pid);
            QueuedQosPacket p;
            p.packet = copyPacket;
            p.id = pid;
            qosPacketQueue.push_back(p);
            qosQueueBytes += copyPacket->getTotalMemoryFootprint();
            locker.unlock();

            if (!clientDisconnected())
            {
                std::shared_ptr<Client> c = makeSharedClient();
                c->writeMqttPacketAndBlameThisClient(*copyPacket.get(), qos);
                copyPacket->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
                count++;
            }
        }
    }
}

// Normatively, this while loop will break on the first element, because all messages are sent out in order and
// should be acked in order.
void Session::clearQosMessage(uint16_t packet_id)
{
#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "Clearing QoS message for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, qosPacketQueue.size());
#endif

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
uint64_t Session::sendPendingQosMessages()
{
    uint64_t count = 0;

    if (!clientDisconnected())
    {
        std::shared_ptr<Client> c = makeSharedClient();
        std::lock_guard<std::mutex> locker(qosQueueMutex);
        for (QueuedQosPacket &qosMessage : qosPacketQueue)
        {
            c->writeMqttPacketAndBlameThisClient(*qosMessage.packet.get(), qosMessage.packet->getQos());
            qosMessage.packet->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
            count++;
        }

        for (const uint16_t packet_id : outgoingQoS2MessageIds)
        {
            PubRel pubRel(packet_id);
            MqttPacket packet(pubRel);
            c->writeMqttPacketAndBlameThisClient(packet, 2);
        }
    }

    return count;
}

std::chrono::time_point<std::chrono::steady_clock> zeroTime;
std::chrono::seconds expireAfter(EXPIRE_SESSION_AFTER);

void Session::touch(std::chrono::time_point<std::chrono::steady_clock> val)
{
    std::chrono::time_point<std::chrono::steady_clock> newval = val > zeroTime ? val : std::chrono::steady_clock::now();
    lastTouched = newval;
}

bool Session::hasExpired()
{
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    return clientDisconnected() && (lastTouched + expireAfter) < now;
}

void Session::addIncomingQoS2MessageId(uint16_t packet_id)
{
    incomingQoS2MessageIds.insert(packet_id);
}

bool Session::incomingQoS2MessageIdInTransit(uint16_t packet_id) const
{
    const auto it = incomingQoS2MessageIds.find(packet_id);
    return it != incomingQoS2MessageIds.end();
}

void Session::removeIncomingQoS2MessageId(u_int16_t packet_id)
{
#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "As QoS 2 receiver: publish released (PUBREL) for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, incomingQoS2MessageIds.size());
#endif

    const auto it = incomingQoS2MessageIds.find(packet_id);
    if (it != incomingQoS2MessageIds.end())
        incomingQoS2MessageIds.erase(it);
}

void Session::addOutgoingQoS2MessageId(uint16_t packet_id)
{
    outgoingQoS2MessageIds.insert(packet_id);
}

void Session::removeOutgoingQoS2MessageId(u_int16_t packet_id)
{
#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "As QoS 2 sender: publish complete (PUBCOMP) for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, outgoingQoS2MessageIds.size());
#endif

    const auto it = outgoingQoS2MessageIds.find(packet_id);
    if (it != outgoingQoS2MessageIds.end())
        outgoingQoS2MessageIds.erase(it);
}
