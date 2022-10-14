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
#include "threadglobals.h"

Session::Session()
{
    const Settings &settings = *ThreadGlobals::getSettings();

    // Sessions also get defaults from the handleConnect() method, but when you create sessions elsewhere, we do need some sensible defaults.
    this->flowControlQuota = settings.maxQosMsgPendingPerClient;
    this->sessionExpiryInterval = settings.expireSessionsAfterSeconds;
}

void Session::increaseFlowControlQuota()
{
    flowControlQuota++;
    this->flowControlQuota = std::min<int>(flowControlQuota, flowControlCealing);
}

void Session::increaseFlowControlQuota(int n)
{
    flowControlQuota += n;
    this->flowControlQuota = std::min<int>(flowControlQuota, flowControlCealing);
}

void Session::clearExpiredMessagesFromQueue()
{
    if (this->lastExpiredMessagesAt + std::chrono::seconds(1) > std::chrono::steady_clock::now())
        return;

    const int n = this->qosPacketQueue.clearExpiredMessages();
    increaseFlowControlQuota(n);
}

bool Session::requiresQoSQueueing() const
{
    const std::shared_ptr<Client> client = makeSharedClient();

    if (!client)
        return true;

    // MQTT 3.1: "Brokers, however, should retry any unacknowledged message."
    // MQTT 3.1.1: "This [reconnecting] is the only circumstance where a Client or Server is REQUIRED to redeliver messages."
    if (client->getProtocolVersion() < ProtocolVersion::Mqtt311)
        return true;

    return !destroyOnDisconnect;
}

void Session::increasePacketId()
{
    nextPacketId++;
    nextPacketId = std::max<uint16_t>(nextPacketId, 1);
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
    this->sessionExpiryInterval = other.sessionExpiryInterval;
    this->willPublish = other.willPublish;
    this->removalQueued = other.removalQueued;
    this->removalQueuedAt = other.removalQueuedAt;


    // TODO: perhaps this copy constructor is nonsense now.

    // TODO: see git history for a change here. We now copy the whole queued publish. Do we want to address that?
    this->qosPacketQueue = other.qosPacketQueue;
}

Session::~Session()
{

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
    this->willPublish = client->getWill();
    this->removalQueued = false;
}

/**
 * @brief Session::writePacket is the main way to give a client a packet -> it goes through the session.
 * @param packet is not const. We set the qos and packet id for each publish. This should be safe, because the packet
 *        with original packet id and qos is not saved. This saves unnecessary copying.
 * @param max_qos
 * @param retain. Keep MQTT-3.3.1-9 in mind: existing subscribers don't get retain=1 on packets.
 * @param count. Reference value is updated. It's for statistics.
 */
void Session::writePacket(PublishCopyFactory &copyFactory, const char max_qos)
{
    assert(max_qos <= 2);

    const char effectiveQos = copyFactory.getEffectiveQos(max_qos);

    const Settings *settings = ThreadGlobals::getSettings();

    Authentication *_auth = ThreadGlobals::getAuth();
    assert(_auth);
    Authentication &auth = *_auth;
    if (auth.aclCheck(client_id, username, copyFactory.getTopic(), copyFactory.getSubtopics(), AclAccess::read, effectiveQos, copyFactory.getRetain(), copyFactory.getUserProperties()) == AuthResult::success)
    {
        std::shared_ptr<Client> c = makeSharedClient();
        if (effectiveQos == 0)
        {
            if (c)
            {
                c->writeMqttPacketAndBlameThisClient(copyFactory, effectiveQos, 0);
            }
        }
        else if (effectiveQos > 0)
        {
            std::unique_lock<std::mutex> locker(qosQueueMutex);

            // We don't clear expired messages for online clients. It would slow down the 'happy flow' and those packets are already in the output
            // buffer, so we can't clear them anyway.
            if (!c)
            {
                clearExpiredMessagesFromQueue();
            }

            if (this->flowControlQuota <= 0 || (qosPacketQueue.getByteSize() >= settings->maxQosBytesPendingPerClient && qosPacketQueue.size() > 0))
            {
                if (QoSLogPrintedAtId != nextPacketId)
                {
                    logger->logf(LOG_WARNING, "Dropping QoS message(s) for client '%s', because it hasn't seen enough PUBACK/PUBCOMP/PUBRECs to release places "
                                              "or it exceeded 'max_qos_bytes_pending_per_client'.", client_id.c_str());
                    QoSLogPrintedAtId = nextPacketId;
                }
                return;
            }

            increasePacketId();
            flowControlQuota--;

            if (requiresQoSQueueing())
                qosPacketQueue.queuePublish(copyFactory, nextPacketId, effectiveQos);

            if (c)
            {
                c->writeMqttPacketAndBlameThisClient(copyFactory, effectiveQos, nextPacketId);
            }
        }
    }
}

/**
 * @brief Session::clearQosMessage clears a QOS message from the queue. Note that in QoS 2, that doesn't complete the handshake.
 * @param packet_id
 * @param qosHandshakeEnds can be set to true when you know the QoS handshake ends, (like) when PUBREC contains an error.
 * @return whether the packet_id in question was found.
 */
bool Session::clearQosMessage(uint16_t packet_id, bool qosHandshakeEnds)
{
#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "Clearing QoS message for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, qosPacketQueue.size());
#endif

    bool result = false;

    std::lock_guard<std::mutex> locker(qosQueueMutex);
    if (requiresQoSQueueing())
        result = qosPacketQueue.erase(packet_id);
    else
    {
        result = true;
    }

    if (qosHandshakeEnds)
    {
        increaseFlowControlQuota();
    }

    return result;
}

/**
 * @brief Session::sendAllPendingQosData sends pending publishes and QoS2 control packets.
 *
 * [MQTT-4.4.0-1] (about MQTT 3.1.1): "When a Client reconnects with CleanSession set to 0, both the Client and Server MUST
 * re-send any unacknowledged PUBLISH Packets (where QoS > 0) and PUBREL Packets using their original Packet Identifiers. This
 * is the only circumstance where a Client or Server is REQUIRED to redeliver messages."
 *
 * Only MQTT 3.1 requires retransmission. MQTT 3.1.1 and MQTT 5 only send on reconnect. At time of writing this comment,
 * FlashMQ doesn't have a retransmission system. I don't think I want to implement one for the sake of 3.1 compliance,
 * because it's just not that great an idea in terms of server load and quality of modern TCP. However, receiving clients
 * can still decide to drop packets, like when their buffers are full. The clients from where the packet originates will
 * never know that, because IT will have received the PUBACK from FlashMQ. The QoS system is not between publisher
 * and subscriber. Users are required to implement something themselves.
 */
void Session::sendAllPendingQosData()
{
    Authentication &authentication = *ThreadGlobals::getAuth();

    std::shared_ptr<Client> c = makeSharedClient();
    if (c)
    {
        std::lock_guard<std::mutex> locker(qosQueueMutex);

        std::shared_ptr<QueuedPublish> qp;
        while ((qp = qosPacketQueue.next()))
        {
            QueuedPublish &queuedPublish = *qp;
            Publish &pub = queuedPublish.getPublish();

            if (pub.hasExpired() || (authentication.aclCheck(pub) != AuthResult::success))
            {
                qosPacketQueue.erase(qp->getPacketId());
                continue;
            }

            if (flowControlQuota <= 0)
            {
                logger->logf(LOG_WARNING, "Dropping QoS message(s) for client '%s', because it exceeds its receive maximum.", client_id.c_str());
                qosPacketQueue.erase(qp->getPacketId());
                continue;
            }

            flowControlQuota--;

            MqttPacket p(c->getProtocolVersion(), pub);
            p.setPacketId(queuedPublish.getPacketId());
            //p.setDuplicate(); // TODO: this is wrong. Until we have a retransmission system, no packets can have the DUP bit set.

            c->writeMqttPacketAndBlameThisClient(p);
        }

        for (const uint16_t packet_id : outgoingQoS2MessageIds)
        {
            PubResponse pubRel(c->getProtocolVersion(), PacketType::PUBREL, ReasonCodes::Success, packet_id);
            MqttPacket packet(pubRel);
            c->writeMqttPacketAndBlameThisClient(packet);
        }
    }
}

bool Session::hasActiveClient() const
{
    return !client.expired();
}

void Session::clearWill()
{
    this->willPublish.reset();
}

std::shared_ptr<WillPublish> &Session::getWill()
{
    return this->willPublish;
}

void Session::setWill(WillPublish &&pub)
{
    this->willPublish = std::make_shared<WillPublish>(std::move(pub));
}

void Session::addIncomingQoS2MessageId(uint16_t packet_id)
{
    assert(packet_id > 0);

    std::unique_lock<std::mutex> locker(qosQueueMutex);
    incomingQoS2MessageIds.insert(packet_id);
}

bool Session::incomingQoS2MessageIdInTransit(uint16_t packet_id)
{
    assert(packet_id > 0);

    std::unique_lock<std::mutex> locker(qosQueueMutex);
    const auto it = incomingQoS2MessageIds.find(packet_id);
    return it != incomingQoS2MessageIds.end();
}

bool Session::removeIncomingQoS2MessageId(u_int16_t packet_id)
{
    assert(packet_id > 0);

    std::unique_lock<std::mutex> locker(qosQueueMutex);

#ifndef NDEBUG
    logger->logf(LOG_DEBUG, "As QoS 2 receiver: publish released (PUBREL) for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, incomingQoS2MessageIds.size());
#endif

    bool result = false;

    const auto it = incomingQoS2MessageIds.find(packet_id);
    if (it != incomingQoS2MessageIds.end())
    {
        incomingQoS2MessageIds.erase(it);
        result = true;
    }

    return result;
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

    increaseFlowControlQuota();
}

/**
 * @brief Session::getDestroyOnDisconnect
 * @return
 *
 * MQTT5: Setting Clean Start to 1 and a Session Expiry Interval of 0, is equivalent to setting CleanSession to 1 in the MQTT Specification Version 3.1.1.
 */
bool Session::getDestroyOnDisconnect() const
{
    return destroyOnDisconnect;
}

void Session::setSessionProperties(uint16_t clientReceiveMax, uint32_t sessionExpiryInterval, bool clean_start, ProtocolVersion protocol_version)
{
    this->flowControlQuota = clientReceiveMax;
    this->flowControlCealing = clientReceiveMax;
    this->sessionExpiryInterval = sessionExpiryInterval;

    if (protocol_version <= ProtocolVersion::Mqtt311 && clean_start)
        destroyOnDisconnect = true;
    else
        destroyOnDisconnect = sessionExpiryInterval == 0;
}

void Session::setSessionExpiryInterval(uint32_t newVal)
{
    // This is only the case on disconnect, but there's no other place where this method is called (so far...)
    if (this->sessionExpiryInterval == 0 && newVal > 0)
    {
        throw ProtocolError("Setting a non-zero session expiry after it was 0 initially is a protocol error.", ReasonCodes::ProtocolError);
    }

    this->sessionExpiryInterval = newVal;
}

void Session::setQueuedRemovalAt()
{
    this->removalQueuedAt = std::chrono::steady_clock::now();
    this->removalQueued = true;
}

uint32_t Session::getSessionExpiryInterval() const
{
    return this->sessionExpiryInterval;
}

uint32_t Session::getCurrentSessionExpiryInterval() const
{
    if (!this->removalQueued || hasActiveClient())
        return this->sessionExpiryInterval;

    const std::chrono::seconds age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - this->removalQueuedAt);
    const uint32_t ageInSeconds = age.count();
    const uint32_t result = ageInSeconds <= this->sessionExpiryInterval ? this->sessionExpiryInterval - age.count() : 0;
    return result;
}

