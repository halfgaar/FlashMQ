#include "rwlockguard.h"

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
    pthread_rwlock_wrlock(rwlock);
}

void RWLockGuard::rdlock()
{
    pthread_rwlock_wrlock(rwlock);
}
