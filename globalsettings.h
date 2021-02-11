#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

// Defaults are defined in ConfigFileParser
struct GlobalSettings
{
    bool allow_unsafe_clientid_chars = false;
    int clientInitialBufferSize = 0;
    int maxPacketSize = 0;
};
#endif // GLOBALSETTINGS_H
