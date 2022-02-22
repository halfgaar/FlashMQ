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
#include "threadglobals.h"

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

bool Session::requiresPacketRetransmission() const
{
    const std::shared_ptr<Client> client = makeSharedClient();

    if (!client)
        return true;

    // MQTT 3.1: "Brokers, however, should retry any unacknowledged message."
    // MQTT 3.1.1: "This [reconnecting] is the only circumstance where a Client or Server is REQUIRED to redeliver messages."
    if (client->getProtocolVersion() < ProtocolVersion::Mqtt311)
        return true;

    // TODO: for MQTT5, the rules are different.
    return !client->getCleanSession();
}

void Session::increasePacketId()
{
    nextPacketId++;
    if (nextPacketId == 0)
        nextPacketId++;
}

/**
 * @brief Session::Session copy constructor. Was created for session storing, and is explicitely kept private, to avoid making accidental copies.
 * @param other
 *
 * Because it was created for session storing, the fields we're copying are the fields being stored.
 */
Session::Session(const Session &other)
{
    // Only the QoS data is modified by worker threads (vs (locked) timed events), so it could change during copying, because
    // it gets called from a separate thread.
    std::unique_lock<std::mutex> locker(qosQueueMutex);

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

/**
 * @brief Session::makeSharedClient get the client of the session, or a null when it has no active current client.
 * @return Returns shared_ptr<Client>, which can contain null when the client has disconnected.
 *
 * The lock() operation is atomic and therefore is the only way to get the current active client without race condition, because
 * typically, this method is called from other client's threads to perform writes, so you have to check validity after
 * obtaining the shared pointer.
 */
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
 * @param packet is not const. We set the qos and packet id for each publish. This should be safe, because the packet
 *        with original packet id and qos is not saved. This saves unnecessary copying.
 * @param max_qos
 * @param retain. Keep MQTT-3.3.1-9 in mind: existing subscribers don't get retain=1 on packets.
 * @param count. Reference value is updated. It's for statistics.
 */
void Session::writePacket(MqttPacket &packet, char max_qos, std::shared_ptr<MqttPacket> &downgradedQos0PacketCopy, uint64_t &count)
{
    assert(max_qos <= 2);
    const char effectiveQos = std::min<char>(packet.getQos(), max_qos);

    const Settings *settings = ThreadGlobals::getSettings();

    Authentication *_auth = ThreadGlobals::getAuth();
    assert(_auth);
    Authentication &auth = *_auth;
    if (auth.aclCheck(client_id, username, packet.getTopic(), packet.getSubtopics(), AclAccess::read, effectiveQos, packet.getRetain()) == AuthResult::success)
    {
        std::shared_ptr<Client> c = makeSharedClient();
        if (effectiveQos == 0)
        {
            if (c)
            {
                const MqttPacket *packetToSend = &packet;

                if (max_qos < packet.getQos())
                {
                    if (!downgradedQos0PacketCopy)
                        downgradedQos0PacketCopy = packet.getCopy(max_qos);
                    packetToSend = downgradedQos0PacketCopy.get();
                }

                count += c->writeMqttPacketAndBlameThisClient(*packetToSend);
            }
        }
        else if (effectiveQos > 0)
        {
            const bool requiresRetransmission = requiresPacketRetransmission();

            if (requiresRetransmission)
            {
                std::unique_lock<std::mutex> locker(qosQueueMutex);

                const size_t totalQosPacketsInTransit = qosPacketQueue.size() + incomingQoS2MessageIds.size() + outgoingQoS2MessageIds.size();
                if (totalQosPacketsInTransit >= settings->maxQosMsgPendingPerClient
                    || (qosPacketQueue.getByteSize() >= settings->maxQosBytesPendingPerClient && qosPacketQueue.size() > 0))
                {
                    if (QoSLogPrintedAtId != nextPacketId)
                    {
                        logger->logf(LOG_WARNING, "Dropping QoS message(s) for client '%s', because max in-transit packet count reached.", client_id.c_str());
                        QoSLogPrintedAtId = nextPacketId;
                    }
                    return;
                }

                increasePacketId();
                std::shared_ptr<MqttPacket> copyPacket = qosPacketQueue.queuePacket(packet, nextPacketId, effectiveQos);

                if (c)
                {
                    count += c->writeMqttPacketAndBlameThisClient(*copyPacket.get());
                    copyPacket->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
                }
            }
            else
            {
                // We don't need to make a copy of the packet in this branch, because:
                // - The packet to give the client won't shrink in size because source and client have a packet_id.
                // - We don't have to store the copy in the session for retransmission, see Session::requiresPacketRetransmission()
                // So, we just keep altering the original published packet.

                std::unique_lock<std::mutex> locker(qosQueueMutex);

                if (qosInFlightCounter >= 65530) // Includes a small safety margin.
                {
                    if (QoSLogPrintedAtId != nextPacketId)
                    {
                        logger->logf(LOG_WARNING, "Dropping QoS message(s) for client '%s', because it hasn't seen enough PUBACKs to release places.", client_id.c_str());
                        QoSLogPrintedAtId = nextPacketId;
                    }
                    return;
                }

                increasePacketId();

                // This changes the packet ID and QoS of the incoming packet for each subscriber, but because we don't store that packet anywhere,
                // that should be fine.
                packet.setPacketId(nextPacketId);
                packet.setQos(effectiveQos);

                qosInFlightCounter++;
                assert(c); // with requiresRetransmission==false, there must be a client.
                count += c->writeMqttPacketAndBlameThisClient(packet);
            }
        }
    }
}

void Session::clearQosMessage(uint16_t packet_id)
{
#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "Clearing QoS message for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, qosPacketQueue.size());
#endif

    std::lock_guard<std::mutex> locker(qosQueueMutex);
    if (requiresPacketRetransmission())
        qosPacketQueue.erase(packet_id);
    else
    {
        qosInFlightCounter--;
        qosInFlightCounter = std::max<int>(0, qosInFlightCounter); // Should never happen, but in case we receive too many PUBACKs.
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

    std::shared_ptr<Client> c = makeSharedClient();
    if (c)
    {
        std::lock_guard<std::mutex> locker(qosQueueMutex);
        for (const std::shared_ptr<MqttPacket> &qosMessage : qosPacketQueue)
        {
            count += c->writeMqttPacketAndBlameThisClient(*qosMessage.get());
            qosMessage->setDuplicate(); // Any dealings with this packet from here will be a duplicate.
        }

        for (const uint16_t packet_id : outgoingQoS2MessageIds)
        {
            PubRel pubRel(packet_id);
            MqttPacket packet(pubRel);
            count += c->writeMqttPacketAndBlameThisClient(packet);
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
    return client.expired() && (lastTouched + expireAfter) < now;
}

void Session::addIncomingQoS2MessageId(uint16_t packet_id)
{
    std::unique_lock<std::mutex> locker(qosQueueMutex);
    incomingQoS2MessageIds.insert(packet_id);
}

bool Session::incomingQoS2MessageIdInTransit(uint16_t packet_id)
{
    std::unique_lock<std::mutex> locker(qosQueueMutex);
    const auto it = incomingQoS2MessageIds.find(packet_id);
    return it != incomingQoS2MessageIds.end();
}

void Session::removeIncomingQoS2MessageId(u_int16_t packet_id)
{
    std::unique_lock<std::mutex> locker(qosQueueMutex);

#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "As QoS 2 receiver: publish released (PUBREL) for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, incomingQoS2MessageIds.size());
#endif

    const auto it = incomingQoS2MessageIds.find(packet_id);
    if (it != incomingQoS2MessageIds.end())
        incomingQoS2MessageIds.erase(it);
}

void Session::addOutgoingQoS2MessageId(uint16_t packet_id)
{
    std::unique_lock<std::mutex> locker(qosQueueMutex);
    outgoingQoS2MessageIds.insert(packet_id);
}

void Session::removeOutgoingQoS2MessageId(u_int16_t packet_id)
{
    std::unique_lock<std::mutex> locker(qosQueueMutex);

#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "As QoS 2 sender: publish complete (PUBCOMP) for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, outgoingQoS2MessageIds.size());
#endif

    const auto it = outgoingQoS2MessageIds.find(packet_id);
    if (it != outgoingQoS2MessageIds.end())
        outgoingQoS2MessageIds.erase(it);
}

bool Session::getCleanSession() const
{
    auto c = client.lock();

    if (!c)
        return false;

    return c->getCleanSession();
}
