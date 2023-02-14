#ifndef PLUGINLOADER_H
#define PLUGINLOADER_H

#include <dlfcn.h>
#include <string>
#include "unistd.h"

enum class PluginFamily
{
    None,
    Determining,
    FlashMQ,
    MosquittoV2,
};

typedef int (*F_plugin_version)(void);

class PluginLoader
{
    PluginFamily pluginFamily = PluginFamily::None;
    int flashmqPluginVersionNumber = 0;

    void* handle = nullptr;
    F_plugin_version version = nullptr;


public:
    PluginLoader();
    PluginLoader(const PluginLoader &other) = delete;

    bool loaded() const;
    void loadPlugin(const std::string &pathToSoFile);
    void *loadSymbol(const char *symbol, bool exceptionOnError = true) const;
    PluginFamily getPluginFamily() const;
    int getFlashMQPluginVersion() const;
};

#endif // PLUGINLOADER_H
