/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef THREADLOOP_H
#define THREADLOOP_H

#include "threaddata.h"
#include "threadglobals.h"

#define MAX_EVENTS 65536

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
