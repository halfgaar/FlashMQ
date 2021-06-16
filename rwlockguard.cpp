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

#include "rwlockguard.h"
#include "utils.h"
#include "stdexcept"

RWLockGuard::RWLockGuard(pthread_rwlock_t *rwlock) :
    rwlock(rwlock)
{

}

RWLockGuard::~RWLockGuard()
{
    unlock();
}

void RWLockGuard::wrlock()
{
    const int rc = pthread_rwlock_wrlock(rwlock);

    if (rc == EDEADLK)
    {
        rwlock = nullptr;
        return;
    }

    if (rc != 0)
        throw std::runtime_error("wrlock failed.");
}

/**
 * @brief RWLockGuard::rdlock locks for reading, and considers it OK of the current thread already owns the lock for writing.
 *
 * The man page says: "The current thread already owns the read-write lock for writing." I hope that is literally the case.
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
