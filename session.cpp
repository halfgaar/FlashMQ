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
#include "threadauth.h"

std::chrono::time_point<std::chrono::steady_clock> appStartTime = std::chrono::steady_clock::now();

Session::Session()
{

}

int64_t Session::getProgramStartedAtUnixTimestamp()
{
    auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::chrono::seconds age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - appStartTime);
    int64_t result = secondsSinceEpoch - age.count();
    return result;
}

void Session::setProgramStartedAtUnixTimestamp(const int64_t unix_timestamp)
{
    auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    const std::chrono::seconds _unix_timestamp = std::chrono::seconds(unix_timestamp);
    const std::chrono::seconds age_in_s = secondsSinceEpoch - _unix_timestamp;
    appStartTime = std::chrono::steady_clock::now() - age_in_s;
}


int64_t Session::getSessionRelativeAgeInMs() const
{
    const std::chrono::milliseconds sessionAge = std::chrono::duration_cast<std::chrono::milliseconds>(lastTouched - appStartTime);
    const int64_t sInMs = sessionAge.count();
    return sInMs;
}

void Session::setSessionTouch(int64_t ageInMs)
{
    std::chrono::milliseconds ms(ageInMs);
    std::chrono::time_point<std::chrono::steady_clock> point = appStartTime + ms;
    lastTouched = point;
}

/**
 * @brief Session::Session copy constructor. Was created for session storing, and is explicitely kept private, to avoid making accidental copies.
 * @param other
 *
 * Because it was created for session storing, the fields we're copying are the fields being stored.
 */
Session::Session(const Session &other)
{
    this->username = other.username;
    this->client_id = other.client_id;
    this->incomingQoS2MessageIds = other.incomingQoS2MessageIds;
    this->outgoingQoS2MessageIds = other.outgoingQoS2MessageIds;
    this->nextPacketId = other.nextPacketId;
    this->lastTouched = other.lastTouched;

    // To be fully correct, we should copy the individual packets, but copying sessions is only done for saving them, and I know
    // that no member of MqttPacket changes in the QoS process, so we can just keep the shared pointer to the original.
    this->qosPacketQueue = other.qosPacketQueue;
}

Session::~Session()
{
    logger->logf(LOG_DEBUG, "Session %s is being destroyed.", getClientId().c_str());
}

std::unique_ptr<Session> Session::getCopy() const
{
    std::unique_ptr<Session> s(new Session(*this));
    return s;
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
}

/**
 * @brief Session::writePacket is the main way to give a client a packet -> it goes through the session.
 * @param packet
 * @param max_qos
 * @param retain. Keep MQTT-3.3.1-9 in mind: existing subscribers don't get retain=1 on packets.
 * @param count. Reference value is updated. It's for statistics.
 */
void Session::writePacket(const MqttPacket &packet, char max_qos, bool retain, uint64_t &count)
{
    assert(max_qos <= 2);
    const char qos = std::min<char>(packet.getQos(), max_qos);

    Authentication *_auth = ThreadAuth::getAuth();
    assert(_auth);
    Authentication &auth = *_auth;
    if (auth.aclCheck(client_id, username, packet.getTopic(), *packet.getSubtopics(), AclAccess::read, qos, retain) == AuthResult::success)
    {
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
            std::unique_lock<std::mutex> locker(qosQueueMutex);

            const size_t totalQosPacketsInTransit = qosPacketQueue.size() + incomingQoS2MessageIds.size() + outgoingQoS2MessageIds.size();
            if (totalQosPacketsInTransit >= MAX_QOS_MSG_PENDING_PER_CLIENT || (qosPacketQueue.getByteSize() >= MAX_QOS_BYTES_PENDING_PER_CLIENT && qosPacketQueue.size() > 0))
            {
                logger->logf(LOG_WARNING, "Dropping QoS message for client '%s', because its QoS buffers were full.", client_id.c_str());
                return;
            }
            nextPacketId++;
            if (nextPacketId == 0)
                nextPacketId++;

            std::shared_ptr<MqttPacket> copyPacket = qosPacketQueue.queuePacket(packet, nextPacketId);
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
    qosPacketQueue.erase(packet_id);
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
        for (const std::shared_ptr<MqttPacket> &qosMessage : qosPacketQueue)
        {
            c->writeMqttPacketAndBlameThisClient(*qosMessage.get(), qosMessage->getQos());
            qosMessage->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
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

/**
 * @brief Session::touch with a time value allowed touching without causing another sys/lib call to get the time.
 * @param newval
 */
void Session::touch(std::chrono::time_point<std::chrono::steady_clock> newval)
{
    lastTouched = newval;
}

void Session::touch()
{
    lastTouched = std::chrono::steady_clock::now();
}

bool Session::hasExpired(int expireAfterSeconds)
{
    std::chrono::seconds expireAfter(expireAfterSeconds);
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
