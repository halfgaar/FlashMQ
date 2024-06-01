/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef SETTINGS_H
#define SETTINGS_H

#include <memory>
#include <list>
#include <limits>
#include <optional>

#include "mosquittoauthoptcompatwrap.h"
#include "listener.h"
#include "network.h"
#include "bridgeconfig.h"

#define ABSOLUTE_MAX_PACKET_SIZE 268435455
#define HEARTBEAT_INTERVAL 1000
#define OVERLOAD_LOGS_MUTE_AFTER_LINES 5000

enum class RetainedMessagesMode
{
    Enabled,
    Downgrade,
    Drop,
    DisconnectWithError
};

enum class SharedSubscriptionTargeting
{
    RoundRobin,
    SenderHash
};

enum class WildcardSubscriptionDenyMode
{
    DenyAll,
    DenyRetainedOnly
};

enum class OverloadMode
{
    Log,
    CloseNewClients
};

class Settings
{
    friend class ConfigFileParser;

    AuthOptCompatWrap authOptCompatWrap;
    std::unordered_map<std::string, std::string> flashmqpluginOpts;

    std::list<std::shared_ptr<BridgeConfig>> bridges;

    void checkUniqueBridgeNames() const;

public:
    // Actual config options with their defaults.
    std::string pluginPath;
    std::string logPath;
    std::optional<bool> quiet;
    bool allowUnsafeClientidChars = false;
    bool allowUnsafeUsernameChars = false;
    bool pluginSerializeInit = false;
    bool pluginSerializeAuthChecks = false;
    bool logSubscriptions = false;
    int clientInitialBufferSize = 1024; // Must be power of 2
    uint32_t maxPacketSize = ABSOLUTE_MAX_PACKET_SIZE;
    uint32_t clientMaxWriteBufferSize = 1048576;
    uint16_t maxIncomingTopicAliasValue = 65535;
    uint16_t maxOutgoingTopicAliasValue = 65535;
#ifdef TESTING
    std::optional<bool> logDebug;
    LogLevel logLevel = LogLevel::Debug;
#else
    std::optional<bool> logDebug;
    LogLevel logLevel = LogLevel::Info;
#endif
    std::string mosquittoPasswordFile;
    std::string mosquittoAclFile;
    bool allowAnonymous = false;
    int rlimitNoFile = 1000000;
    uint32_t expireSessionsAfterSeconds = 1209600;
    uint32_t expireRetainedMessagesAfterSeconds = std::numeric_limits<uint32_t>::max();
    int pluginTimerPeriod = 60;
    std::string storageDir;
    int threadCount = 0;
    uint16_t maxQosMsgPendingPerClient = 512;
    uint maxQosBytesPendingPerClient = 65536;
    bool willsEnabled = true;
    uint32_t retainedMessagesDeliveryLimit = 2048;
    uint32_t retainedMessagesNodeLimit = std::numeric_limits<uint32_t>::max();
    std::chrono::seconds retainedMessageNodeLifetime = std::chrono::seconds(0);
    RetainedMessagesMode retainedMessagesMode = RetainedMessagesMode::Enabled;
    SharedSubscriptionTargeting sharedSubscriptionTargeting = SharedSubscriptionTargeting::RoundRobin;
    uint16_t minimumWildcardSubscriptionDepth = 0;
    WildcardSubscriptionDenyMode wildcardSubscriptionDenyMode = WildcardSubscriptionDenyMode::DenyAll;
    bool zeroByteUsernameIsAnonymous = false;
    std::chrono::milliseconds maxEventLoopDrift = std::chrono::milliseconds(2000);
    OverloadMode overloadMode = OverloadMode::Log;
    std::chrono::milliseconds setRetainedMessageDeferTimeout = std::chrono::milliseconds(0);
    std::chrono::milliseconds setRetainedMessageDeferTimeoutSpread = std::chrono::milliseconds(1000);
    std::list<std::shared_ptr<Listener>> listeners; // Default one is created later, when none are defined.

    std::list<Network> setRealIpFrom;

    AuthOptCompatWrap &getAuthOptsCompat();
    std::unordered_map<std::string, std::string> &getFlashmqpluginOpts();

    std::string getRetainedMessagesDBFile() const;
    std::string getSessionsDBFile() const;
    std::string getBridgeNamesDBFile() const;

    uint32_t getExpireSessionAfterSeconds() const;

    bool matchAddrWithSetRealIpFrom(const struct sockaddr *addr) const;
    bool matchAddrWithSetRealIpFrom(const struct sockaddr_in6 *addr) const;
    bool matchAddrWithSetRealIpFrom(const struct sockaddr_in *addr) const;

    std::list<std::shared_ptr<BridgeConfig>> stealBridges();
};

#endif // SETTINGS_H
