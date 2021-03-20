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

public:
    // Actual config options with their defaults.
    std::string authPluginPath;
    std::string logPath;
    bool allowUnsafeClientidChars = false;
    bool allowUnsafeUsernameChars = false;
    bool authPluginSerializeInit = false;
    bool authPluginSerializeAuthChecks = false;
    int clientInitialBufferSize = 1024; // Must be power of 2
    int maxPacketSize = 268435461; // 256 MB + 5
    bool logDebug = false;
    bool logSubscriptions = false;
    std::list<std::shared_ptr<Listener>> listeners; // Default one is created later, when none are defined.

    AuthOptCompatWrap &getAuthOptsCompat();
};

#endif // SETTINGS_H
