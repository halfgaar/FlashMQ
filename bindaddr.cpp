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
#include <sys/socket.h>
#include <sys/stat.h>
#include "utils.h"
#include "logger.h"


BindAddr::BindAddr(
        int family, const std::string &bindAddress, int port,
        const std::optional<std::string> &user, const std::optional<std::string> &group, const std::optional<mode_t> &mode
    ) :
    unixsock_user(user),
    unixsock_group(group),
    unixsock_mode(mode)
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

        this->unixsock_path = bindAddress;
    }
}

void BindAddr::bind_socket(int socket_fd)
{
    check<std::runtime_error>(bind(socket_fd, get(), len));

    if (family == AF_UNIX)
    {
        FMQ_ENSURE(this->unixsock_path);

        std::optional<uid_t> uid;
        std::optional<gid_t> gid;

        if (unixsock_user)
        {
            std::optional<unsigned long> parsed_uid = try_stoul(unixsock_user.value());

            if (parsed_uid)
                uid = parsed_uid.value();
            else
            {
                std::optional<SysUserFields> data = get_pw_name(unixsock_user.value());
                if (data)
                    uid = data->uid;
            }

            if (!uid)
                Logger::getInstance()->log(LOG_WARNING) << "Could not owner as name or uid '" << unixsock_user.value() << "' on " << unixsock_path.value() << ".";
        }

        if (unixsock_group)
        {
            std::optional<unsigned long> parsed_gid = try_stoul(unixsock_group.value());

            if (parsed_gid)
                gid = parsed_gid.value();
            else
            {
                std::optional<SysGroupFields> data = get_gr_name(unixsock_group.value());
                if (data)
                    gid = data->gid;
            }

            if (!gid)
                Logger::getInstance()->log(LOG_WARNING) << "Could not group as name or gid '" << unixsock_group.value() << "' on " << unixsock_path.value() << ".";
        }

        if (uid || gid)
        {
            if (chown(unixsock_path.value().c_str(), uid.value_or(-1), gid.value_or(-1)) < 0)
            {
                Logger::getInstance()->log(LOG_WARNING) << "Can't change owner/group of '" << unixsock_path.value() << "'. Reason: " << strerror(errno) << ".";
            }
        }

        if (unixsock_mode)
        {
            if (chmod(unixsock_path.value().c_str(), this->unixsock_mode.value()) < 0)
            {
                Logger::getInstance()->log(LOG_WARNING) << "Can't change mode of '" << unixsock_path.value() << "'. Reason: " << strerror(errno) << ".";
            }
        }
    }
}

