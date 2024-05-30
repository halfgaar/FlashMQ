/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "rwlockguard.h"
#include "utils.h"
#include <stdexcept>

RWLockGuard::RWLockGuard(pthread_rwlock_t *rwlock) :
    rwlock(rwlock)
{

}

RWLockGuard::~RWLockGuard()
{
    unlock();
}

bool RWLockGuard::trywrlock()
{
    const int rc = pthread_rwlock_trywrlock(rwlock);

    if (rc == 0)
        return true;

    if (rc == EINVAL)
        throw std::runtime_error("Lock not initialized.");

    rwlock = nullptr;
    return false;
}

/**
 * @brief RWLockGuard::wrlock locks for writing i.e. exclusive lock.
 *
 * Contrary to rdlock, we don't accept deadlock errors, because you may be trying to upgrade a read lock to a write lock, which will
 * cause actualy dead locks.
 */
void RWLockGuard::wrlock()
{
    const int rc = pthread_rwlock_wrlock(rwlock);

    if (rc != 0)
        throw std::runtime_error("wrlock failed.");
}

bool RWLockGuard::tryrdlock()
{
    const int rc = pthread_rwlock_tryrdlock(rwlock);

    if (rc == 0)
        return true;

    if (rc == EINVAL)
        throw std::runtime_error("Lock not initialized.");

    rwlock = nullptr;
    return false;
}

/**
 * @brief RWLockGuard::rdlock locks for reading, and considers it OK of the current thread already owns the lock for writing.
 *
 * The pthread_rwlock_rdlock man page says: "EDEADLK: The current thread already owns the read-write lock for writing." I hope
 * that is literally the case.
 */
void RWLockGuard::rdlock()
{
    int rc = pthread_rwlock_rdlock(rwlock);

    if (rc == EDEADLK)
    {
        rwlock = NULL;
        return;
    }

    if (rc != 0)
        throw std::runtime_error(strerror(rc));
}

void RWLockGuard::unlock()
{
    if (rwlock != NULL)
    {
        pthread_rwlock_unlock(rwlock);
        rwlock = NULL;
    }
}
