/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef FLASHMQTESTCLIENT_H
#define FLASHMQTESTCLIENT_H

#include <thread>
#include <memory>

#include "pluginloader.h"
#include "settings.h"
#include "threaddata.h"
#include "checkedweakptr.h"
#include "mutexowned.h"

class SubAckIsError : public std::runtime_error
{
public:
    SubAckIsError(const std::string &msg) : std::runtime_error(msg) {}
};

/**
 * @brief The FlashMQTestClient class uses the existing server code as a client, for testing purposes.
 */
class FlashMQTestClient
{
    struct ReceivedObjects
    {
        std::vector<MqttPacket> receivedPackets;
        std::vector<MqttPacket> receivedPublishes;

        void clear();
    };

    std::shared_ptr<PluginLoader> pluginLoader = std::make_shared<PluginLoader>();
    Settings settings;
    ThreadDataOwner testServerWorkerThreadData;
    CheckedWeakPtr<Client> client_weak;
    std::shared_ptr<WillPublish> will;

    static int clientCount;

    void waitForCondition(std::function<bool()> f, int timeout = 1);


public:
    MutexOwned<ReceivedObjects> receivedObjects;

    FlashMQTestClient();
    ~FlashMQTestClient();

    void start();
    void connectClient(ProtocolVersion protocolVersion, int port=21883, bool _waitForConnack=true);
    void connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval, int port=21883, bool _waitForConnack=true);
    void connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval, std::function<void(Connect&)> manipulateConnect,
                       int port=21883, bool _waitForConnack=true);
    void subscribe(const std::string topic, uint8_t qos, bool noLocal=false, bool retainAsPublished=false, uint32_t subscriptionIdentifier=0,
                   RetainHandling retainHandling=RetainHandling::SendRetainedMessagesAtSubscribe);
    void unsubscribe(const std::string &topic);
    void publish(const std::string &topic, const std::string &payload, uint8_t qos);
    void publish(Publish &pub);
    void writeAuth(const Auth &auth);
    void clearReceivedLists();
    void setWill(std::shared_ptr<WillPublish> &will);
    void disconnect(ReasonCodes reason);

    void waitForQuit();
    void waitForConnack();
    void waitForDisconnectPacket();
    void waitForMessageCount(const size_t count, int timeout = 1);
    void waitForPacketCount(const size_t count, int timeout = 1);

    std::shared_ptr<Client> getClient();
    std::string getClientId();
    ProtocolVersion getProtocolVersion();
    bool clientExpired() const;
};

#endif // FLASHMQTESTCLIENT_H
