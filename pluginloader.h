/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef PLUGINLOADER_H
#define PLUGINLOADER_H

#include <dlfcn.h>
#include <string>
#include <unistd.h>
#include <unordered_map>

enum class PluginFamily
{
    None,
    Determining,
    FlashMQ,
    MosquittoV2,
};

typedef int (*F_plugin_version)(void);
typedef void(*F_flashmq_plugin_main_init_v1)(std::unordered_map<std::string, std::string> &auth_opts);
typedef void(*F_flashmq_plugin_main_deinit_v1)(std::unordered_map<std::string, std::string> &auth_opts);

class PluginLoader
{
    PluginFamily pluginFamily = PluginFamily::None;
    int flashmqPluginVersionNumber = 0;

    void* handle = nullptr;
    F_plugin_version version = nullptr;
    F_flashmq_plugin_main_init_v1 main_init_v1 = nullptr;
    F_flashmq_plugin_main_deinit_v1 main_deinit_v1 = nullptr;

public:
    PluginLoader();
    PluginLoader(const PluginLoader &other) = delete;

    bool loaded() const;
    void loadPlugin(const std::string &pathToSoFile);
    void *loadSymbol(const char *symbol, bool exceptionOnError = true) const;
    PluginFamily getPluginFamily() const;
    int getFlashMQPluginVersion() const;

    void mainInit(std::unordered_map<std::string, std::string> &plugin_opts);
    void mainDeinit(std::unordered_map<std::string, std::string> &plugin_opts);
};

#endif // PLUGINLOADER_H
