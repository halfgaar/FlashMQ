/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
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
    std::chrono::time_point<std::chrono::steady_clock> lastExpiredMessagesAt = std::chrono::steady_clock::now();

    /**
     * Even though flow control data is not part of the session state, I'm keeping it here because there are already
     * mutexes that they can be placed under, saving additional synchronization.
     */
    int flowControlCealing = 0xFFFF;
    int flowControlQuota = 0xFFFF;

    uint32_t sessionExpiryInterval = 0;
    uint16_t QoSLogPrintedAtId = 0;
    bool destroyOnDisconnect = false;
    std::shared_ptr<WillPublish> willPublish;
    bool removalQueued = false;
    ClientType clientType = ClientType::Normal;
    std::chrono::time_point<std::chrono::steady_clock> removalQueuedAt;
    Logger *logger = Logger::getInstance();

    void clearExpiredMessagesFromQueue();

    void increaseFlowControlQuota();
    void increaseFlowControlQuota(int n);
    bool requiresQoSQueueing() const;
    uint16_t getNextPacketId();

    Session(const Session &other) = delete;
public:
    Session();

    Session(Session &&other) = delete;
    ~Session();

    const std::string &getClientId() const { return client_id; }
    std::shared_ptr<Client> makeSharedClient() const;
    void assignActiveConnection(std::shared_ptr<Client> &client);
    PacketDropReason writePacket(PublishCopyFactory &copyFactory, const uint8_t max_qos, bool retainAsPublished);
    bool clearQosMessage(uint16_t packet_id, bool qosHandshakeEnds);
    void sendAllPendingQosData();
    bool hasActiveClient() const;
    void clearWill();
    std::shared_ptr<WillPublish> &getWill();
    void setWill(WillPublish &&pub);
    ClientType getClientType() const { return clientType; }
    void setClientType(ClientType val);

    void addIncomingQoS2MessageId(uint16_t packet_id);
    bool incomingQoS2MessageIdInTransit(uint16_t packet_id);
    bool removeIncomingQoS2MessageId(u_int16_t packet_id);
    void addOutgoingQoS2MessageId(uint16_t packet_id);
    void removeOutgoingQoS2MessageId(u_int16_t packet_id);
    void increaseFlowControlQuotaLocked();
    uint16_t getNextPacketIdLocked();

    bool getDestroyOnDisconnect() const;

    void setSessionProperties(uint16_t clientReceiveMax, uint32_t sessionExpiryInterval, bool clean_start, ProtocolVersion protocol_version);
    void setSessionExpiryInterval(uint32_t newVal);
    void setQueuedRemovalAt();
    uint32_t getSessionExpiryInterval() const;
    uint32_t getCurrentSessionExpiryInterval() const;
};

#endif // SESSION_H
