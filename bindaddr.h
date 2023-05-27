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
#include <memory>

/**
 * @brief The BindAddr struct helps creating the resource for bind(). It uses an intermediate struct sockaddr to avoid compiler warnings, and
 * this class helps a bit with resource management of it.
 */
struct BindAddr
{
    std::unique_ptr<sockaddr> p;
    socklen_t len = 0;
};

#endif // BINDADDR_H
