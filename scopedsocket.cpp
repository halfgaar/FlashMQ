/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "scopedsocket.h"
#include <stdexcept>
#include <cassert>
#include "utils.h"

ScopedSocket::ScopedSocket(int socket, const std::string &unixSocketPath, const std::shared_ptr<Listener> &listener) :
    socket(socket),
    unixSocketPath(unixSocketPath),
    listener(listener)
{
    if (this->socket < 0)
        throw std::runtime_error("Cannot create scoped socket");
}

ScopedSocket::ScopedSocket(ScopedSocket &&other)
{
    assert(this != &other);
    *this = std::move(other);
}

ScopedSocket::~ScopedSocket()
{
    if (socket >= 0)
        close(socket);
    socket = -1;
    listener.reset();

    unlink_if_sock(unixSocketPath);
}

int ScopedSocket::get() const
{
    return socket;
}

ScopedSocket &ScopedSocket::operator=(ScopedSocket &&other)
{
    assert(this != &other);

    if (this->socket >= 0)
    {
        close(this->socket);
        this->socket = -1;
    }

    this->listener = std::move(other.listener);

    this->socket = other.socket;
    other.socket = -1;

    this->unixSocketPath = std::move(other.unixSocketPath);
    other.unixSocketPath.clear();

    return *this;
}

std::shared_ptr<Listener> ScopedSocket::getListener() const
{
    return listener.lock();
}
