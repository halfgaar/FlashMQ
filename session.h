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
#include <shared_mutex>
#include <set>

#include "forward_declarations.h"
#include "logger.h"
#include "sessionsandsubscriptionsdb.h"
#include "qospacketqueue.h"
#include "publishcopyfactory.h"
#include "lockedweakptr.h"
#include "lockedsharedptr.h"
#include "mutexowned.h"

class Session
{
#ifdef TESTING
    friend class MainTests;
#endif

    struct QoSData
    {
        QoSPublishQueue qosPacketQueue;
        std::set<uint16_t> incomingQoS2MessageIds;
        std::set<uint16_t> outgoingQoS2MessageIds;
        uint16_t nextPacketId = 0;
        uint16_t QoSLogPrintedAtId = 0;
        std::chrono::time_point<std::chrono::steady_clock> lastExpiredMessagesAt = std::chrono::steady_clock::now();

        /**
         * Even though flow control data is not part of the session state, I'm keeping it here because there are already
         * mutexes that they can be placed under, saving additional synchronization.
         */
        int flowControlCealing = 0xFFFF;
        int flowControlQuota = 0xFFFF;

        QoSData(const uint16_t maxQosMsgPendingPerClient) :
            flowControlQuota(maxQosMsgPendingPerClient)
        {

        }

        void clearExpiredMessagesFromQueue();
        void increaseFlowControlQuota();
        void increaseFlowControlQuota(int n);
        uint16_t getNextPacketId();
    };

    friend class SessionsAndSubscriptionsDB;

    /*
     * THREADING WARNING
     *
     * Sessions are accessed cross-thread. Use unprotected primitives, atomics, MutexOwned objects, or
     * const-constructed objects with careful consideration.
     */

    LockedWeakPtr<Client> client;
    const std::string client_id;
    const std::string username;
    MutexOwned<QoSData> qos;

    // Note, we set these write-once to avoid threading issues. As a work-around to avoid mutexing in a hot path.
    std::optional<std::string> local_prefix;
    std::optional<std::string> remote_prefix;

    std::mutex clientSwitchMutex;

    uint32_t sessionExpiryInterval = 0;
    bool destroyOnDisconnect = false;
    LockedSharedPtr<WillPublish> willPublish;
    bool removalQueued = false;
    ClientType clientType = ClientType::Normal;
    std::chrono::time_point<std::chrono::steady_clock> removalQueuedAt;
    Logger *logger = Logger::getInstance();

    Session(const Session &other) = delete;
public:
    Session(const std::string &clientid, const std::string &username);

    Session(Session &&other) = delete;
    ~Session();

    const std::string &getClientId() const { return client_id; }
    const std::string &getUsername() const { return username; }
    std::shared_ptr<Client> makeSharedClient();
    void assignActiveConnection(const std::shared_ptr<Client> &client);
    void assignActiveConnection(const std::shared_ptr<Session> &thisSession, const std::shared_ptr<Client> &client,
                                uint16_t clientReceiveMax, uint32_t sessionExpiryInterval, bool clean_start);
    PacketDropReason writePacket(PublishCopyFactory &copyFactory, const uint8_t max_qos, bool retainAsPublished, const uint32_t subscriptionIdentifier);
    bool clearQosMessage(uint16_t packet_id, bool qosHandshakeEnds);
    void sendAllPendingQosData();
    bool hasActiveClient();
    void clearWill();
    std::shared_ptr<WillPublish> getWill();
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
    uint32_t getCurrentSessionExpiryInterval();

    void setLocalPrefix(const std::optional<std::string> &s);
    void setRemotePrefix(const std::optional<std::string> &s);
    const std::optional<std::string> &getLocalPrefix() const { return local_prefix; }
    const std::optional<std::string> &getRemotePrefix() const { return remote_prefix; }
};

#endif // SESSION_H
