/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <cassert>

#include "session.h"
#include "client.h"
#include "threadglobals.h"
#include "threadglobals.h"
#include "exceptions.h"
#include "plugin.h"
#include "settings.h"

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

    /*
     * MQTT 3.1: "Brokers, however, should retry any unacknowledged message."
     * MQTT 3.1.1: "This [reconnecting] is the only circumstance where a Client or Server is REQUIRED to redeliver messages."
     *
     * I disabled it, because MQTT 3.1 retransmission has not been implemented on purpose, because it's a legacy idea that
     * doesn't work well in practice.
     */
    /*
    if (client->getProtocolVersion() < ProtocolVersion::Mqtt311)
        return true;
    */

    return !destroyOnDisconnect;
}

/**
 * @brief get next packet ID and decrease the flow control counter. Remember to increase the flow control counter in the proper places.
 * @return
 *
 * You should also check that flowControl is higher than 0 before you use this.
 */
uint16_t Session::getNextPacketId()
{
    nextPacketId++;
    nextPacketId = std::max<uint16_t>(nextPacketId, 1);
    assert(flowControlQuota > 0);
    flowControlQuota--;
    return nextPacketId;
}

Session::~Session()
{
    logger->log(LOG_DEBUG) << "Session destructor of session with client ID '" << this->client_id << "'.";
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
PacketDropReason Session::writePacket(PublishCopyFactory &copyFactory, const uint8_t max_qos, bool retainAsPublished)
{
    assert(max_qos <= 2);

    const std::shared_ptr<Client> c = makeSharedClient();

    const uint8_t effectiveQos = copyFactory.getEffectiveQos(max_qos);
    retainAsPublished = retainAsPublished || clientType == ClientType::Mqtt3DefactoBridge;
    bool effectiveRetain = copyFactory.getEffectiveRetain(retainAsPublished);

    if (c && !c->isRetainedAvailable())
        effectiveRetain = false;

    const Settings *settings = ThreadGlobals::getSettings();
    Authentication *auth = ThreadGlobals::getAuth();
    assert(auth);

    const AuthResult aclResult = auth->aclCheck(client_id, username, copyFactory.getTopic(), copyFactory.getSubtopics(), copyFactory.getPayload(), AclAccess::read,
                                                effectiveQos, effectiveRetain, copyFactory.getUserProperties());

    if (aclResult != AuthResult::success)
    {
        return PacketDropReason::AuthDenied;
    }

    uint16_t pack_id = 0;

    if (__builtin_expect(effectiveQos > 0, 0))
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
                                          "or it exceeded the queue size. You could increase 'max_qos_msg_pending_per_client' "
                                          "or 'max_qos_bytes_pending_per_client' (but this is also subject the client's 'receive max').", client_id.c_str());
                QoSLogPrintedAtId = nextPacketId;
            }
            return PacketDropReason::QoSTODOSomethingSomething;
        }

        pack_id = getNextPacketId();

        if (requiresQoSQueueing())
            qosPacketQueue.queuePublish(copyFactory, pack_id, effectiveQos, effectiveRetain);
    }

    PacketDropReason return_value = PacketDropReason::ClientOffline;

    if (c)
    {
        return_value = c->writeMqttPacketAndBlameThisClient(copyFactory, effectiveQos, pack_id, effectiveRetain);
    }

    return return_value;
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
        std::vector<std::shared_ptr<QueuedPublish>> copiedPublishes;
        std::vector<uint16_t> copiedQoS2Ids;

        {
            std::lock_guard<std::mutex> locker(qosQueueMutex);

            std::shared_ptr<QueuedPublish> qp_ = qosPacketQueue.getTail();;
            while (qp_)
            {
                std::shared_ptr<QueuedPublish> qp = qp_;
                qp_ = qp_->next;

                Publish &pub = qp->getPublish();

                if (pub.hasExpired() || (authentication.aclCheck(pub, pub.payload) != AuthResult::success))
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

                copiedPublishes.push_back(qp);
            }

            for (const uint16_t packet_id : outgoingQoS2MessageIds)
            {
                copiedQoS2Ids.push_back(packet_id);
            }
        }

        for(std::shared_ptr<QueuedPublish> &qp : copiedPublishes)
        {
            Publish &pub = qp->getPublish();
            PublishCopyFactory fac(&pub);
            const bool retain = !c->isRetainedAvailable() ? false : pub.retain;
            c->writeMqttPacketAndBlameThisClient(fac, pub.qos, qp->getPacketId(), retain);
        }

        for(uint16_t id : copiedQoS2Ids)
        {
            PubResponse pubRel(c->getProtocolVersion(), PacketType::PUBREL, ReasonCodes::Success, id);
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

void Session::setClientType(ClientType val)
{
    this->clientType = val;
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

void Session::increaseFlowControlQuotaLocked()
{
    std::unique_lock<std::mutex> locker(qosQueueMutex);
    increaseFlowControlQuota();
}

uint16_t Session::getNextPacketIdLocked()
{
    std::unique_lock<std::mutex> locker(qosQueueMutex);
    return getNextPacketId();
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
    std::unique_lock<std::mutex> locker(qosQueueMutex);

    // Flow control is not part of the session state, so/but/and because we call this function every time a client connects, we reset it properly.
    this->flowControlQuota = clientReceiveMax;
    this->flowControlCealing = clientReceiveMax;

    this->sessionExpiryInterval = sessionExpiryInterval;

    if (protocol_version <= ProtocolVersion::Mqtt311)
        destroyOnDisconnect = clean_start;
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

