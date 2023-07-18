/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef SESSIONSANDSUBSCRIPTIONSDB_H
#define SESSIONSANDSUBSCRIPTIONSDB_H

#include <list>
#include <memory>

#include "forward_declarations.h"
#include "persistencefile.h"
#include "types.h"

#define MAGIC_STRING_SESSION_FILE_V1 "FlashMQRetainedDBv1" // That this is called 'retained' was a bug...
#define MAGIC_STRING_SESSION_FILE_V2 "FlashMQSessionDBv2"
#define MAGIC_STRING_SESSION_FILE_V3 "FlashMQSessionDBv3"
#define MAGIC_STRING_SESSION_FILE_V4 "FlashMQSessionDBv4"
#define MAGIC_STRING_SESSION_FILE_V5 "FlashMQSessionDBv5"
#define RESERVED_SPACE_SESSIONS_DB_V2 32

/**
 * @brief The SubscriptionForSerializing struct contains the fields we're interested in when saving a subscription.
 */
struct SubscriptionForSerializing
{
    const std::string clientId;
    const uint8_t qos = 0;
    const std::string shareName;
    const bool noLocal = false;
    const bool retainAsPublished = false;

    SubscriptionForSerializing(const std::string &clientId, uint8_t qos, bool noLocal, bool retainAsPublished);
    SubscriptionForSerializing(const std::string &clientId, uint8_t qos, bool noLocal, bool retainAsPublished, const std::string &shareName);
    SubscriptionForSerializing(const std::string &&clientId, uint8_t qos, bool noLocal, bool retainAsPublished);

    SubscriptionForSerializing(const std::string &&clientId, SubscriptionOptionsByte options, const std::string &shareName);

    SubscriptionOptionsByte getSubscriptionOptions() const;
};

struct SessionsAndSubscriptionsResult
{
    std::list<std::shared_ptr<Session>> sessions;
    std::unordered_map<std::string, std::list<SubscriptionForSerializing>> subscriptions;
};


class SessionsAndSubscriptionsDB : public PersistenceFile
{
    enum class ReadVersion
    {
        unknown,
        v1,
        v2,
        v3,
        v4,
        v5
    };

    ReadVersion readVersion = ReadVersion::unknown;

    SessionsAndSubscriptionsResult readDataV3V4V5();
    void writeRowHeader();
public:
    SessionsAndSubscriptionsDB(const std::string &filePath);

    void openWrite();
    void openRead();

    void saveData(const std::vector<std::shared_ptr<Session>> &sessions, const std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &subscriptions);
    SessionsAndSubscriptionsResult readData();
};

#endif // SESSIONSANDSUBSCRIPTIONSDB_H
