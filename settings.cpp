/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "settings.h"
#include "utils.h"

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
