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
    if (pthread_rwlock_wrlock(rwlock) != 0)
        throw std::runtime_error("wrlock failed.");
}

void RWLockGuard::rdlock()
{
    if (pthread_rwlock_wrlock(rwlock) != 0)
        throw std::runtime_error("rdlock failed.");
}

void RWLockGuard::unlock()
{
    if (rwlock != NULL)
    {
        pthread_rwlock_unlock(rwlock);
        rwlock = NULL;
    }
}
