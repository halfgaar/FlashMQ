/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

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
        const char *err = dlerror();
        std::string errmsg = err ? err : "";
        throw FatalError(errmsg);
    }

    if ((version = (F_plugin_version)loadSymbol("flashmq_plugin_version", false)) != nullptr)
    {
        pluginFamily = PluginFamily::FlashMQ;
        flashmqPluginVersionNumber = version();

        if (flashmqPluginVersionNumber < 1 || flashmqPluginVersionNumber  > 4)
        {
            throw FatalError("This FlashMQ version only support plugin version 1, 2, 3 or 4.");
        }
    }
    else if ((version = (F_plugin_version)loadSymbol("mosquitto_auth_plugin_version", false)) != nullptr)
    {
        if (version() != 2)
        {
            throw FatalError("Only Mosquitto plugin version 2 is supported at this time.");
        }

        pluginFamily = PluginFamily::MosquittoV2;
    }
    else
    {
        throw FatalError("This does not seem to be a FlashMQ native plugin or Mosquitto plugin version 2.");
    }

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

    main_init_v1 = (F_flashmq_plugin_main_init_v1)loadSymbol("flashmq_plugin_main_init", false);
    main_deinit_v1 = (F_flashmq_plugin_main_deinit_v1)loadSymbol("flashmq_plugin_main_deinit", false);
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

    std::optional<std::string> error;

    try
    {
        main_init_v1(plugin_opts);
    }
    catch(std::exception &ex)
    {
        error = ex.what();
        Logger *logger = Logger::getInstance();
        logger->log(LOG_ERR) << "Exception in flashmq_plugin_main_init(): " << ex.what();
    }

    if (error)
        throw std::runtime_error(error.value());
}

void PluginLoader::mainDeinit(std::unordered_map<std::string, std::string> &plugin_opts)
{
    if (!main_deinit_v1)
        return;

    try
    {
        main_deinit_v1(plugin_opts);
    }
    catch(std::exception &ex)
    {
        Logger *logger = Logger::getInstance();
        logger->log(LOG_ERR) << "Exception in flashmq_plugin_main_deinit(): " << ex.what();
    }
}
