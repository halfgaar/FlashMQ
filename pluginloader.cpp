#include "pluginloader.h"

#include "logger.h"

#include "exceptions.h"

PluginLoader::PluginLoader()
{

}

bool PluginLoader::loaded() const
{
    return handle != nullptr;
}

void PluginLoader::loadPlugin(const std::string &pathToSoFile)
{
    if (pathToSoFile.empty())
        return;

    Logger *logger = Logger::getInstance();

    logger->logf(LOG_NOTICE, "Loading auth plugin %s", pathToSoFile.c_str());

    pluginFamily = PluginFamily::Determining;

    if (access(pathToSoFile.c_str(), R_OK) != 0)
    {
        std::ostringstream oss;
        oss << "Error loading auth plugin: The file " << pathToSoFile << " is not there or not readable";
        throw FatalError(oss.str());
    }

    handle = dlopen(pathToSoFile.c_str(), RTLD_LAZY|RTLD_GLOBAL);

    if (handle == NULL)
    {
        std::string errmsg(dlerror());
        throw FatalError(errmsg);
    }

    version = (F_plugin_version)loadSymbol("mosquitto_auth_plugin_version", false);
    if (version != nullptr)
    {
        if (version() != 2)
        {
            throw FatalError("Only Mosquitto plugin version 2 is supported at this time.");
        }

        pluginFamily = PluginFamily::MosquittoV2;
    }
    else if ((version = (F_plugin_version)loadSymbol("flashmq_plugin_version", false)) != nullptr)
    {
        pluginFamily = PluginFamily::FlashMQ;
        flashmqPluginVersionNumber = version();

        if (flashmqPluginVersionNumber != 1)
        {
            throw FatalError("FlashMQ plugin only supports version 1.");
        }
    }
    else
    {
        throw FatalError("This does not seem to be a FlashMQ native plugin or Mosquitto plugin version 2.");
    }

    main_init_v1 = (F_flashmq_plugin_main_init_v1)loadSymbol("flashmq_plugin_main_init", false);

    if (dlclose(handle) != 0)
    {
        std::string errmsg(dlerror());
        throw FatalError(errmsg);
    }
    version = nullptr;

    handle = dlopen(pathToSoFile.c_str(), RTLD_NOW|RTLD_GLOBAL);

    if (handle == NULL)
    {
        std::string errmsg(dlerror());
        throw FatalError(errmsg);
    }
}

void *PluginLoader::loadSymbol(const char *symbol, bool exceptionOnError) const
{
    void *r = dlsym(handle, symbol);

    if (r == NULL && exceptionOnError)
    {
        std::string errmsg(dlerror());
        throw FatalError(errmsg);
    }

    return r;
}

PluginFamily PluginLoader::getPluginFamily() const
{
    return this->pluginFamily;
}

int PluginLoader::getFlashMQPluginVersion() const
{
    return this->flashmqPluginVersionNumber;
}

void PluginLoader::mainInit(std::unordered_map<std::string, std::string> &plugin_opts)
{
    if (!main_init_v1)
        return;

    main_init_v1(plugin_opts);
}
