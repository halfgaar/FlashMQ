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

/**
 * @brief The ScopedSocket struct allows for a bit of RAII and move semantics on a socket fd.
 */
struct ScopedSocket
{
    int socket = 0;
    ScopedSocket(int socket);
    ScopedSocket(ScopedSocket &&other);
    ~ScopedSocket();
};

#endif // SCOPEDSOCKET_H
