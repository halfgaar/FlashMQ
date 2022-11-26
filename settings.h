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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <memory>
#include <list>
#include <limits>

#include "mosquittoauthoptcompatwrap.h"
#include "listener.h"

#define ABSOLUTE_MAX_PACKET_SIZE 268435455

enum class RetainedMessagesMode
{
    Enabled,
    Downgrade,
    Drop,
    DisconnectWithError
};

class Settings
{
    friend class ConfigFileParser;

    AuthOptCompatWrap authOptCompatWrap;
    std::unordered_map<std::string, std::string> flashmqpluginOpts;

public:
    // Actual config options with their defaults.
    std::string pluginPath;
    std::string logPath;
    bool quiet = false;
    bool allowUnsafeClientidChars = false;
    bool allowUnsafeUsernameChars = false;
    bool pluginSerializeInit = false;
    bool pluginSerializeAuthChecks = false;
    int clientInitialBufferSize = 1024; // Must be power of 2
    int maxPacketSize = ABSOLUTE_MAX_PACKET_SIZE;
    uint16_t maxIncomingTopicAliasValue = 65535;
    uint16_t maxOutgoingTopicAliasValue = 65535;
#ifdef TESTING
    bool logDebug = true;
#else
    bool logDebug = false;
#endif
    bool logSubscriptions = false;
    std::string mosquittoPasswordFile;
    std::string mosquittoAclFile;
    bool allowAnonymous = false;
    int rlimitNoFile = 1000000;
    uint32_t expireSessionsAfterSeconds = 1209600;
    uint32_t expireRetainedMessagesAfterSeconds = std::numeric_limits<uint32_t>::max();
    uint32_t expireRetainedMessagesTimeBudgetMs = 300;
    int pluginTimerPeriod = 60;
    std::string storageDir;
    int threadCount = 0;
    uint16_t maxQosMsgPendingPerClient = 512;
    uint maxQosBytesPendingPerClient = 65536;
    bool willsEnabled = true;
    RetainedMessagesMode retainedMessagesMode = RetainedMessagesMode::Enabled;
    std::list<std::shared_ptr<Listener>> listeners; // Default one is created later, when none are defined.

    AuthOptCompatWrap &getAuthOptsCompat();
    std::unordered_map<std::string, std::string> &getFlashmqpluginOpts();

    std::string getRetainedMessagesDBFile() const;
    std::string getSessionsDBFile() const;

    uint32_t getExpireSessionAfterSeconds() const;
};

#endif // SETTINGS_H
