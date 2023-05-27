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

#include <vector>
#include "forward_declarations.h"

class VectorClearGuard
{
    std::vector<MqttPacket> &v;
public:
    VectorClearGuard(std::vector<MqttPacket> &v) :
        v(v)
    {

    }

    ~VectorClearGuard()
    {
        v.clear();
    }
};

void do_thread_work(ThreadData *threadData);


#endif // THREADLOOP_H
