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

BindAddr::BindAddr(BindAddr &&other)
{
    if (this == &other)
        return;

    this->p = other.p;
    this->len = other.len;
    other.p = nullptr;
    other.len = 0;
}

BindAddr::BindAddr(int family)
{
    if (!(family == AF_INET || family == AF_INET6))
        return;

    if (family == AF_INET)
    {
        this->len = sizeof(struct sockaddr_in);
    }
    else if (family == AF_INET6)
    {
        this->len = sizeof(struct sockaddr_in6);
    }

    if (this->len == 0)
        return;

    this->p = reinterpret_cast<sockaddr*>(malloc(this->len));

    if (this->p == nullptr)
        throw std::bad_alloc();

    memset(this->p, 0, this->len);
    p->sa_family = family;
}

BindAddr::~BindAddr()
{
    free(p);
    p = nullptr;
    len = 0;
}
