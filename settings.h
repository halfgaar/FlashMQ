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

#include "mosquittoauthoptcompatwrap.h"
#include "listener.h"

class Settings
{
    friend class ConfigFileParser;

    AuthOptCompatWrap authOptCompatWrap;
    std::unordered_map<std::string, std::string> flashmqAuthPluginOpts;

public:
    // Actual config options with their defaults.
    std::string authPluginPath;
    std::string logPath;
    bool quiet = false;
    bool allowUnsafeClientidChars = false;
    bool allowUnsafeUsernameChars = false;
    bool authPluginSerializeInit = false;
    bool authPluginSerializeAuthChecks = false;
    int clientInitialBufferSize = 1024; // Must be power of 2
    int maxPacketSize = 268435461; // 256 MB + 5
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
    uint64_t expireSessionsAfterSeconds = 1209600;
    int authPluginTimerPeriod = 60;
    std::string storageDir;
    int threadCount = 0;
    uint maxQosMsgPendingPerClient = 512;
    uint maxQosBytesPendingPerClient = 65536;
    std::list<std::shared_ptr<Listener>> listeners; // Default one is created later, when none are defined.

    AuthOptCompatWrap &getAuthOptsCompat();
    std::unordered_map<std::string, std::string> &getFlashmqAuthPluginOpts();

    std::string getRetainedMessagesDBFile() const;
    std::string getSessionsDBFile() const;
};

#endif // SETTINGS_H
