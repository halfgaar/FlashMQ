/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef RWLOCKGUARD_H
#define RWLOCKGUARD_H


#include <pthread.h>

class RWLockGuard
{
    pthread_rwlock_t *rwlock = NULL;
public:
    RWLockGuard(pthread_rwlock_t *rwlock);
    ~RWLockGuard();
    bool trywrlock();
    void wrlock();
    bool tryrdlock();
    void rdlock();
    void unlock();
};

#endif // RWLOCKGUARD_H
