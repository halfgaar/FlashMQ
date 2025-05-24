/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef SCOPEDSOCKET_H
#define SCOPEDSOCKET_H

#include <fcntl.h>
#include <unistd.h>
#include "listener.h"

/**
 * @brief The ScopedSocket struct allows for a bit of RAII and move semantics on a socket fd.
 */
class ScopedSocket
{
    int socket = -1;
    std::string unixSocketPath;
    std::weak_ptr<Listener> listener;
public:
    ScopedSocket() = default;
    ScopedSocket(int socket, const std::string &unixSocketPath, const std::shared_ptr<Listener> &listener);
    ScopedSocket(const ScopedSocket &other) = delete;
    ScopedSocket(ScopedSocket &&other);
    ~ScopedSocket();
    int get() const;
    ScopedSocket &operator=(ScopedSocket &&other);
    std::shared_ptr<Listener> getListener() const;
};

#endif // SCOPEDSOCKET_H
