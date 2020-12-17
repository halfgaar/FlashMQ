#include "rwlockguard.h"
#include "utils.h"
#include "stdexcept"

RWLockGuard::RWLockGuard(pthread_rwlock_t *rwlock) :
    rwlock(rwlock)
{

}

RWLockGuard::~RWLockGuard()
{
    pthread_rwlock_unlock(rwlock);
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
