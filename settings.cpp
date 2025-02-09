/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <unordered_set>

#include "exceptions.h"
#include "settings.h"
#include "utils.h"

void Settings::checkUniqueBridgeNames() const
{
    std::unordered_set<std::string> prefixes;

    for (const BridgeConfig &bridge : bridges)
    {
        const std::string &prefix = bridge.clientidPrefix;

        if (prefixes.find(bridge.clientidPrefix) != prefixes.end())
        {
            std::string err = formatString("Value '%s' is not unique. All bridge prefixes must be unique.", prefix.c_str());
            throw ConfigFileException(err);
        }

        prefixes.insert(prefix);
    }
}

AuthOptCompatWrap &Settings::getAuthOptsCompat()
{
    return authOptCompatWrap;
}

std::unordered_map<std::string, std::string> &Settings::getFlashmqpluginOpts()
{
    return this->flashmqpluginOpts;
}

std::string Settings::getRetainedMessagesDBFile() const
{
    if (storageDir.empty())
        return "";

    std::string path = formatString("%s/%s", storageDir.c_str(), "retained.db");
    return path;
}

std::string Settings::getSessionsDBFile() const
{
    if (storageDir.empty())
        return "";

    std::string path = formatString("%s/%s", storageDir.c_str(), "sessions.db");
    return path;
}

std::string Settings::getBridgeNamesDBFile() const
{
    if (storageDir.empty())
        return "";

    std::string path = formatString("%s/%s", storageDir.c_str(), "bridgenames.db");
    return path;
}

/**
 * @brief because 0 means 'forever', we have to translate this.
 * @return
 */
uint32_t Settings::getExpireSessionAfterSeconds() const
{
    return expireSessionsAfterSeconds > 0 ? expireSessionsAfterSeconds : std::numeric_limits<uint32_t>::max();
}

bool Settings::matchAddrWithSetRealIpFrom(const sockaddr *addr) const
{
    return std::any_of(setRealIpFrom.begin(), setRealIpFrom.end(), [=](const Network &n) { return n.match(addr);});
}

bool Settings::matchAddrWithSetRealIpFrom(const sockaddr_in6 *addr) const
{
    return matchAddrWithSetRealIpFrom(reinterpret_cast<const struct sockaddr*>(addr));
}

bool Settings::matchAddrWithSetRealIpFrom(const sockaddr_in *addr) const
{
    return matchAddrWithSetRealIpFrom(reinterpret_cast<const struct sockaddr*>(addr));
}

std::list<BridgeConfig> Settings::stealBridges()
{
    std::list<BridgeConfig> result = std::move(this->bridges);
    this->bridges.clear();
    return result;
}
