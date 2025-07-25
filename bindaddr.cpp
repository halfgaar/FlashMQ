/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "bindaddr.h"
#include <stdlib.h>
#include <cstring>
#include <new>
#include <sys/un.h>
#include <stdexcept>



BindAddr::BindAddr(int family, const std::string &bindAddress, int port)
{
    if (!(family == AF_INET || family == AF_INET6 || family == AF_UNIX))
        throw std::exception();

    if (family == AF_UNIX)
    {
        if (bindAddress.empty())
            throw std::runtime_error("Binding to a unix socket requires a path.");
    }
    else
    {
        if (port <= 0 || port > 0xFFFF)
            throw std::runtime_error("IP listen port invalid.");
    }

    this->family = family;

    if (family == AF_INET)
    {
        struct sockaddr_in in_addr_v4;
        std::memset(&in_addr_v4, 0, sizeof(in_addr_v4));

        this->len = sizeof(in_addr_v4);

        if (bindAddress.empty())
            in_addr_v4.sin_addr.s_addr = INADDR_ANY;
        else
            inet_pton(AF_INET, bindAddress.c_str(), &in_addr_v4.sin_addr);

        in_addr_v4.sin_port = htons(port);
        in_addr_v4.sin_family = AF_INET;

        std::memcpy(dat.data(), &in_addr_v4, sizeof(in_addr_v4));
    }
    if (family == AF_INET6)
    {
        struct sockaddr_in6 in_addr_v6;
        std::memset(&in_addr_v6, 0, sizeof(in_addr_v6));

        this->len = sizeof(in_addr_v6);

        if (bindAddress.empty())
            in_addr_v6.sin6_addr = IN6ADDR_ANY_INIT;
        else
            inet_pton(AF_INET6, bindAddress.c_str(), &in_addr_v6.sin6_addr);

        in_addr_v6.sin6_port = htons(port);
        in_addr_v6.sin6_family = AF_INET6;

        std::memcpy(dat.data(), &in_addr_v6, sizeof(in_addr_v6));
    }
    if (family == AF_UNIX)
    {
        struct sockaddr_un path;
        std::memset(&path, 0, sizeof(path));

        this->len = sizeof(path);

        if (bindAddress.length() > 100)
            throw std::runtime_error("Unix domain socket path can't be longer than 100 chars.");

        path.sun_family = AF_UNIX;

        std::memcpy(path.sun_path, bindAddress.data(), bindAddress.size());
        std::memcpy(dat.data(), &path, sizeof(path));
    }
}

