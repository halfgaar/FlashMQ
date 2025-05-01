/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef THREADLOOP_H
#define THREADLOOP_H

#define MAX_EVENTS 65536

#include <cstdint>
#include <vector>
#include "forward_declarations.h"

template<typename T>
class VectorClearGuard
{
    std::vector<T> &v;
public:
    VectorClearGuard(std::vector<T> &v) :
        v(v)
    {

    }

    ~VectorClearGuard()
    {
        v.clear();
    }
};

struct ReadyClient
{
    uint32_t events;
    std::shared_ptr<Client> client;

    ReadyClient() = delete;
    ReadyClient(const ReadyClient &other) = delete;
    ReadyClient(ReadyClient &&other) = default;
    ReadyClient(uint32_t events, std::shared_ptr<Client> &&client);
};

void do_thread_work(std::shared_ptr<ThreadData> threadData);


#endif // THREADLOOP_H
