/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef BINDADDR_H
#define BINDADDR_H

#include <arpa/inet.h>
#include <string>
#include <vector>
#include <optional>

/**
 * @brief The BindAddr struct helps creating the resource for bind(). It uses an intermediate struct sockaddr to avoid compiler
 * warnings and type aliasing violations, and this class helps a bit with resource management of it.
 */
class BindAddr
{
    std::vector<char> dat = std::vector<char>(sizeof (struct sockaddr_storage));
    sa_family_t family = AF_UNSPEC;
    socklen_t len = 0;

    std::optional<std::string> unixsock_path;
    const std::optional<std::string> unixsock_user;
    const std::optional<std::string> unixsock_group;
    const std::optional<mode_t> unixsock_mode;

public:

    BindAddr() = delete;
    BindAddr(
        int family, const std::string &bindAddress, int port,
        const std::optional<std::string> &user={}, const std::optional<std::string> &group={}, const std::optional<mode_t> &mode={});
    BindAddr(const BindAddr &other) = delete;
    BindAddr(BindAddr &&other) = delete;
    BindAddr &operator=(const BindAddr &other) = delete;
    BindAddr &operator=(BindAddr &&other) = delete;
    void bind_socket(int socket_fd);

    const sockaddr *get() const
    {
        return reinterpret_cast<const sockaddr*>(dat.data());
    }

    socklen_t getLen() const { return len; }
};

#endif // BINDADDR_H
