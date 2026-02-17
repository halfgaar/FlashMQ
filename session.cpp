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
#include "threaddata.h"
#include "exceptions.h"
#include "plugin.h"
#include "settings.h"

Session::Session(const std::string &clientid, const std::string &username, const std::optional<std::string> &fmq_client_group_id) :
    client_id(clientid),
    username(username),
    fmq_client_group_id(fmq_client_group_id),

    // Sessions also get defaults from the handleConnect() method, but when you create sessions elsewhere, we do need some sensible defaults.
    qos(ThreadGlobals::getSettings()->maxQosMsgPendingPerClient)
{
    this->sessionExpiryInterval = ThreadGlobals::getSettings()->expireSessionsAfterSeconds;
}

void Session::QoSData::increaseFlowControlQuota()
{
    flowControlQuota++;
    flowControlQuota = std::min<int>(flowControlQuota, flowControlCealing);
}

void Session::QoSData::increaseFlowControlQuota(int n)
{
    flowControlQuota += n;
    flowControlQuota = std::min<int>(flowControlQuota, flowControlCealing);
}

void Session::QoSData::clearExpiredMessagesFromQueue()
{
    const auto now = std::chrono::steady_clock::now();
    if (lastExpiredMessagesAt + std::chrono::seconds(1) > now)
        return;

    lastExpiredMessagesAt = now;

    const int n = qosPacketQueue.clearExpiredMessages();
    increaseFlowControlQuota(n);
}

/**
 * @brief get next packet ID and decrease the flow control counter. Remember to increase the flow control counter in the proper places.
 * @return
 *
 * You should also check that flowControl is higher than 0 before you use this.
 */
uint16_t Session::QoSData::getNextPacketId()
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
std::shared_ptr<Client> Session::makeSharedClient()
{
    return client.lock();
}

void Session::assignActiveConnection(const std::shared_ptr<Client> &client)
{
    this->client = client;
    this->willPublish = client->getWill();
    this->removalQueued = false;
}

void Session::assignActiveConnection(const std::shared_ptr<Session> &thisSession, const std::shared_ptr<Client> &client,
                                     uint16_t clientReceiveMax, uint32_t sessionExpiryInterval, bool clean_start)
{
    assert(this == thisSession.get());

    std::lock_guard<std::mutex> locker(this->clientSwitchMutex);

    if (username != client->getUsername())
        throw ProtocolError("Cannot take over session with different username", ReasonCodes::NotAuthorized);

    thisSession->assignActiveConnection(client);
    client->assignSession(thisSession);
    thisSession->setSessionProperties(clientReceiveMax, sessionExpiryInterval, clean_start, client->getProtocolVersion());
}

/**
 * @brief Session::writePacket is the main way to give a client a packet -> it goes through the session.
 * @param packet is not const. We set the qos and packet id for each publish. This should be safe, because the packet
 *        with original packet id and qos is not saved. This saves unnecessary copying.
 * @param max_qos
 * @param retain. Keep MQTT-3.3.1-9 in mind: existing subscribers don't get retain=1 on packets.
 * @param count. Reference value is updated. It's for statistics.
 */
PacketDropReason Session::writePacket(PublishCopyFactory &copyFactory, const uint8_t max_qos, bool retainAsPublished, const uint32_t subscriptionIdentifier)
{
    /*
     * We want to do as little as possible before the ACL check, because it's code that's called
     * exponentially for subscribers that don't have access to topics, like wildcard subscribers.
     */

    assert(max_qos <= 2);

    const uint8_t effectiveQos = copyFactory.getEffectiveQos(max_qos);
    retainAsPublished = retainAsPublished || clientType == ClientType::Mqtt3DefactoBridge;
    bool effectiveRetain = copyFactory.getEffectiveRetain(retainAsPublished);

    const AuthResult aclResult = ThreadGlobals::getThreadData()->authentication.aclCheck(
        client_id, username, copyFactory.getTopic(), copyFactory.getSubtopics(), "", copyFactory.getPayload(), AclAccess::read,
        effectiveQos, effectiveRetain, copyFactory.getCorrelationData(), copyFactory.getResponseTopic(), copyFactory.getContentType(),
        copyFactory.getExpiresAt(), copyFactory.getUserProperties());

    if (aclResult != AuthResult::success)
    {
        return PacketDropReason::AuthDenied;
    }

    const std::shared_ptr<Client> c = makeSharedClient();

    std::optional<std::string> topic_override;

    {
        // The size check is to prevent making "local/haha/" into "" when the local_prefix is "local/haha/"
        if (local_prefix && startsWith(copyFactory.getTopic(), *local_prefix) && copyFactory.getTopic().size() > local_prefix->size())
        {
            topic_override = copyFactory.getTopic();
            topic_override->erase(0, local_prefix->length());
        }

        if (remote_prefix)
        {
            const std::string &to_append = topic_override.value_or(copyFactory.getTopic());
            topic_override = *remote_prefix + to_append;
        }
    }

    uint16_t pack_id = 0;

    if (__builtin_expect(effectiveQos > 0, 0))
    {
        const Settings *settings = ThreadGlobals::getSettings();
        MutexLocked<QoSData> qos_locked = qos.lock();

        // We don't clear expired messages for online clients. It would slow down the 'happy flow' and those packets are already in the output
        // buffer, so we can't clear them anyway.
        if (!c)
        {
            qos_locked->clearExpiredMessagesFromQueue();
        }

        if (qos_locked->flowControlQuota <= 0 || (qos_locked->qosPacketQueue.getByteSize() >= settings->maxQosBytesPendingPerClient && qos_locked->qosPacketQueue.size() > 0))
        {
            if (qos_locked->QoSLogPrintedAtId != qos_locked->nextPacketId)
            {
                if (c)
                {
                    logger->log(LOG_WARNING) <<
                        "Dropping QoS message(s) for on-line client '" << client_id << "', because it hasn't seen "
                        "enough PUBACK/PUBCOMP/PUBRECs to release places "
                        "or it exceeded the queue size. You could increase 'max_qos_msg_pending_per_client' "
                        "or 'max_qos_bytes_pending_per_client' (but this is also subject the client's 'receive max').";
                }
                else
                {
                    logger->log(LOG_WARNING) <<
                        "Dropping QoS message(s) for off-line client '" << client_id << "', because the limit has been reached. "
                        "You can increase 'max_qos_msg_pending_per_client' and/or 'max_qos_bytes_pending_per_client' to buffer more.";
                }
                qos_locked->QoSLogPrintedAtId = qos_locked->nextPacketId;
            }
            return PacketDropReason::QoSTODOSomethingSomething;
        }

        pack_id = qos_locked->getNextPacketId();

        if (!destroyOnDisconnect)
            qos_locked->qosPacketQueue.queuePublish(copyFactory, pack_id, effectiveQos, effectiveRetain, subscriptionIdentifier, topic_override);
    }

    PacketDropReason return_value = PacketDropReason::ClientOffline;

    if (c)
    {
        if (!c->isRetainedAvailable())
            effectiveRetain = false;

        return_value = c->writeMqttPacketAndBlameThisClient(copyFactory, effectiveQos, pack_id, effectiveRetain, subscriptionIdentifier, topic_override);
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
    bool result = false;

    MutexLocked<QoSData> qos_locked = qos.lock();

    if (logger->wouldLog(LOG_PUBLISH))
        logger->logf(LOG_PUBLISH, "Clearing QoS message for '%s', packet id '%d'. Left in queue: %d", client_id.c_str(), packet_id, qos_locked->qosPacketQueue.size());

    if (!destroyOnDisconnect)
        result = qos_locked->qosPacketQueue.erase(packet_id);
    else
    {
        result = true;
    }

    if (qosHandshakeEnds)
    {
        qos_locked->increaseFlowControlQuota();
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
    Authentication &authentication = ThreadGlobals::getThreadData()->authentication;

    std::shared_ptr<Client> c = makeSharedClient();
    if (c)
    {
        std::vector<std::tuple<Publish, std::optional<std::string>, uint16_t>> copiedPublishes;
        std::vector<uint16_t> copiedQoS2Ids;

        {
            MutexLocked<QoSData> qos_locked = qos.lock();

            std::shared_ptr<QueuedPublish> qp_ = qos_locked->qosPacketQueue.getTail();
            while (qp_)
            {
                std::shared_ptr<QueuedPublish> qp = qp_;
                qp_ = qp_->next.lock();

                Publish &pub = qp->getPublish();

                if (pub.hasExpired() || (authentication.aclCheck(pub, pub.payload) != AuthResult::success))
                {
                    qos_locked->qosPacketQueue.erase(qp->getPacketId());
                    continue;
                }

                if (qos_locked->flowControlQuota <= 0)
                {
                    logger->logf(LOG_WARNING, "Dropping QoS message(s) for client '%s', because it exceeds its receive maximum.", client_id.c_str());
                    qos_locked->qosPacketQueue.erase(qp->getPacketId());
                    continue;
                }

                qos_locked->flowControlQuota--;

                copiedPublishes.emplace_back(pub, qp->getTopicOverride(), qp->getPacketId());
            }

            for (const uint16_t packet_id : qos_locked->outgoingQoS2MessageIds)
            {
                copiedQoS2Ids.push_back(packet_id);
            }
        }

        for(std::tuple<Publish, std::optional<std::string>, uint16_t> &p : copiedPublishes)
        {
            Publish &pub = std::get<Publish>(p);
            PublishCopyFactory fac(&pub);
            const bool retain = !c->isRetainedAvailable() ? false : pub.retain;
            c->writeMqttPacketAndBlameThisClient(fac, pub.qos, std::get<uint16_t>(p), retain, pub.subscriptionIdentifier, std::get<std::optional<std::string>>(p));
        }

        for(uint16_t id : copiedQoS2Ids)
        {
            PubResponse pubRel(c->getProtocolVersion(), PacketType::PUBREL, ReasonCodes::Success, id);
            MqttPacket packet(pubRel);
            c->writeMqttPacketAndBlameThisClient(packet);
        }
    }
}

bool Session::hasActiveClient()
{
    return !client.expired();
}

void Session::clearWill()
{
    this->willPublish.reset();
}

std::shared_ptr<WillPublish> Session::getWill()
{
    return this->willPublish.getCopy();
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

    MutexLocked<QoSData> qos_locked = qos.lock();
    qos_locked->incomingQoS2MessageIds.insert(packet_id);
}

bool Session::incomingQoS2MessageIdInTransit(uint16_t packet_id)
{
    assert(packet_id > 0);

    MutexLocked<QoSData> qos_locked = qos.lock();
    const auto it = qos_locked->incomingQoS2MessageIds.find(packet_id);
    return it != qos_locked->incomingQoS2MessageIds.end();
}

bool Session::removeIncomingQoS2MessageId(u_int16_t packet_id)
{
    assert(packet_id > 0);

    MutexLocked<QoSData> qos_locked = qos.lock();

    if (logger->wouldLog(LOG_PUBLISH))
    {
        logger->logf(LOG_PUBLISH, "As QoS 2 receiver: publish released (PUBREL) for '%s', packet id '%d'. Left in queue: %d",
                     client_id.c_str(), packet_id, qos_locked->incomingQoS2MessageIds.size());
    }

    bool result = false;

    const auto it = qos_locked->incomingQoS2MessageIds.find(packet_id);
    if (it != qos_locked->incomingQoS2MessageIds.end())
    {
        qos_locked->incomingQoS2MessageIds.erase(it);
        result = true;
    }

    return result;
}

void Session::addOutgoingQoS2MessageId(uint16_t packet_id)
{
    MutexLocked<QoSData> qos_locked = qos.lock();
    qos_locked->outgoingQoS2MessageIds.insert(packet_id);
}

void Session::removeOutgoingQoS2MessageId(u_int16_t packet_id)
{
    MutexLocked<QoSData> qos_locked = qos.lock();

    if (logger->wouldLog(LOG_PUBLISH))
    {
        logger->logf(LOG_PUBLISH, "As QoS 2 sender: publish complete (PUBCOMP) for '%s', packet id '%d'. Left in queue: %d",
                     client_id.c_str(), packet_id, qos_locked->outgoingQoS2MessageIds.size());
    }

    const auto it = qos_locked->outgoingQoS2MessageIds.find(packet_id);
    if (it != qos_locked->outgoingQoS2MessageIds.end())
        qos_locked->outgoingQoS2MessageIds.erase(it);

    qos_locked->increaseFlowControlQuota();
}

void Session::increaseFlowControlQuotaLocked()
{
    MutexLocked<QoSData> qos_locked = qos.lock();
    qos_locked->increaseFlowControlQuota();
}

uint16_t Session::getNextPacketIdLocked()
{
    MutexLocked<QoSData> qos_locked = qos.lock();
    return qos_locked->getNextPacketId();
}

void Session::resetQoSData()
{
    MutexLocked<QoSData> qos_locked = qos.lock();
    QoSData &q = *qos_locked;
    QoSData new_q(ThreadGlobals::getSettings()->maxQosMsgPendingPerClient);
    q = std::move(new_q);
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
    MutexLocked<QoSData> qos_locked = qos.lock();

    // Flow control is not part of the session state, so/but/and because we call this function every time a client connects, we reset it properly.
    qos_locked->flowControlQuota = clientReceiveMax;
    qos_locked->flowControlCealing = clientReceiveMax;

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

uint32_t Session::getCurrentSessionExpiryInterval()
{
    if (!this->removalQueued || hasActiveClient())
        return this->sessionExpiryInterval;

    const std::chrono::seconds age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - this->removalQueuedAt);
    const uint32_t ageInSeconds = age.count();
    const uint32_t result = ageInSeconds <= this->sessionExpiryInterval ? this->sessionExpiryInterval - age.count() : 0;
    return result;
}

void Session::setLocalPrefix(const std::optional<std::string> &s)
{
    // A work-around for prefixes of session being accessed cross-thread. The std::optional sets the 'engaged' flag last, so this works.
    if (this->local_prefix)
        return;

    this->local_prefix = s;
}

void Session::setRemotePrefix(const std::optional<std::string> &s)
{
    // A work-around for prefixes of session being accessed cross-thread. The std::optional sets the 'engaged' flag last, so this works.
    if (this->remote_prefix)
        return;

    this->remote_prefix = s;
}

