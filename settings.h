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
    int clientInitialBufferSize = 1024; // Must be power of 2
    int maxPacketSize = 268435461; // 256 MB + 5
    std::list<std::shared_ptr<Listener>> listeners; // Default one is created later, when none are defined.

    AuthOptCompatWrap &getAuthOptsCompat();
};

#endif // SETTINGS_H
