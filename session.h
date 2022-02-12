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

class Session
{
#ifdef TESTING
    friend class MainTests;
#endif

    friend class SessionsAndSubscriptionsDB;

    std::weak_ptr<Client> client;
    std::string client_id;
    std::string username;
    QoSPacketQueue qosPacketQueue;
    std::set<uint16_t> incomingQoS2MessageIds;
    std::set<uint16_t> outgoingQoS2MessageIds;
    std::mutex qosQueueMutex;
    uint16_t nextPacketId = 0;
    uint16_t qosInFlightCounter = 0;
    uint16_t QoSLogPrintedAtId = 0;
    std::chrono::time_point<std::chrono::steady_clock> lastTouched = std::chrono::steady_clock::now();
    Logger *logger = Logger::getInstance();

    int64_t getSessionRelativeAgeInMs() const;
    void setSessionTouch(int64_t ageInMs);
    bool requiresPacketRetransmission() const;
    void increasePacketId();

    Session(const Session &other);
public:
    Session();

    Session(Session &&other) = delete;
    ~Session();

    static int64_t getProgramStartedAtUnixTimestamp();
    static void setProgramStartedAtUnixTimestamp(const int64_t unix_timestamp);

    std::unique_ptr<Session> getCopy() const;

    const std::string &getClientId() const { return client_id; }
    std::shared_ptr<Client> makeSharedClient() const;
    void assignActiveConnection(std::shared_ptr<Client> &client);
    void writePacket(MqttPacket &packet, char max_qos, std::shared_ptr<MqttPacket> &downgradedQos0PacketCopy, uint64_t &count);
    void clearQosMessage(uint16_t packet_id);
    uint64_t sendPendingQosMessages();
    void touch(std::chrono::time_point<std::chrono::steady_clock> val);
    void touch();
    bool hasExpired(int expireAfterSeconds);

    void addIncomingQoS2MessageId(uint16_t packet_id);
    bool incomingQoS2MessageIdInTransit(uint16_t packet_id);
    void removeIncomingQoS2MessageId(u_int16_t packet_id);

    void addOutgoingQoS2MessageId(uint16_t packet_id);
    void removeOutgoingQoS2MessageId(u_int16_t packet_id);

    bool getCleanSession() const;
};

#endif // SESSION_H
