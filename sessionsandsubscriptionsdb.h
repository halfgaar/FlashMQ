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

#ifndef SESSIONSANDSUBSCRIPTIONSDB_H
#define SESSIONSANDSUBSCRIPTIONSDB_H

#include <list>
#include <memory>

#include "forward_declarations.h"
#include "persistencefile.h"

#define MAGIC_STRING_SESSION_FILE_V1 "FlashMQRetainedDBv1" // That this is called 'retained' was a bug...
#define MAGIC_STRING_SESSION_FILE_V2 "FlashMQSessionDBv2"
#define MAGIC_STRING_SESSION_FILE_V3 "FlashMQSessionDBv3"
#define MAGIC_STRING_SESSION_FILE_V4 "FlashMQSessionDBv4"
#define RESERVED_SPACE_SESSIONS_DB_V2 32

/**
 * @brief The SubscriptionForSerializing struct contains the fields we're interested in when saving a subscription.
 */
struct SubscriptionForSerializing
{
    const std::string clientId;
    const uint8_t qos = 0;
    const std::string shareName;

    SubscriptionForSerializing(const std::string &clientId, uint8_t qos);
    SubscriptionForSerializing(const std::string &clientId, uint8_t qos, const std::string &shareName);
    SubscriptionForSerializing(const std::string &&clientId, uint8_t qos);
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
        v4
    };

    ReadVersion readVersion = ReadVersion::unknown;

    SessionsAndSubscriptionsResult readDataV3V4();
    void writeRowHeader();
public:
    SessionsAndSubscriptionsDB(const std::string &filePath);

    void openWrite();
    void openRead();

    void saveData(const std::vector<std::shared_ptr<Session>> &sessions, const std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &subscriptions);
    SessionsAndSubscriptionsResult readData();
};

#endif // SESSIONSANDSUBSCRIPTIONSDB_H
