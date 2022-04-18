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

#ifndef SESSION_H
#define SESSION_H

#include <memory>
#include <list>
#include <mutex>
#include <set>

#include "forward_declarations.h"
#include "logger.h"
#include "sessionsandsubscriptionsdb.h"
#include "qospacketqueue.h"
#include "publishcopyfactory.h"

class Session
{
#ifdef TESTING
    friend class MainTests;
#endif

    friend class SessionsAndSubscriptionsDB;

    std::weak_ptr<Client> client;
    std::string client_id;
    std::string username;
    QoSPublishQueue qosPacketQueue;
    std::set<uint16_t> incomingQoS2MessageIds;
    std::set<uint16_t> outgoingQoS2MessageIds;
    std::mutex qosQueueMutex;
    uint16_t nextPacketId = 0;
    uint16_t qosInFlightCounter = 0;
    uint32_t sessionExpiryInterval = 0;
    uint16_t maxQosMsgPending;
    uint16_t QoSLogPrintedAtId = 0;
    bool destroyOnDisconnect = false;
    std::shared_ptr<WillPublish> willPublish;
    Logger *logger = Logger::getInstance();

    bool requiresPacketRetransmission() const;
    void increasePacketId();

    Session(const Session &other);
public:
    Session();

    Session(Session &&other) = delete;
    ~Session();

    std::unique_ptr<Session> getCopy() const;

    const std::string &getClientId() const { return client_id; }
    std::shared_ptr<Client> makeSharedClient() const;
    void assignActiveConnection(std::shared_ptr<Client> &client);
    void writePacket(PublishCopyFactory &copyFactory, const char max_qos, uint64_t &count);
    void clearQosMessage(uint16_t packet_id);
    uint64_t sendPendingQosMessages();
    bool hasActiveClient() const;
    void clearWill();
    std::shared_ptr<WillPublish> &getWill();
    void setWill(WillPublish &&pub);

    void addIncomingQoS2MessageId(uint16_t packet_id);
    bool incomingQoS2MessageIdInTransit(uint16_t packet_id);
    void removeIncomingQoS2MessageId(u_int16_t packet_id);

    void addOutgoingQoS2MessageId(uint16_t packet_id);
    void removeOutgoingQoS2MessageId(u_int16_t packet_id);

    bool getDestroyOnDisconnect() const;

    void setSessionProperties(uint16_t maxQosPackets, uint32_t sessionExpiryInterval, bool clean_start, ProtocolVersion protocol_version);
    void setSessionExpiryInterval(uint32_t newVal);
    uint32_t getSessionExpiryInterval() const;
};

#endif // SESSION_H
