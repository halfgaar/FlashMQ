#ifndef RWLOCKGUARD_H
#define RWLOCKGUARD_H


#include <pthread.h>

class RWLockGuard
{
    pthread_rwlock_t *rwlock = NULL;
public:
    RWLockGuard(pthread_rwlock_t *rwlock);
    ~RWLockGuard();
    void wrlock();
    void rdlock();
    void unlock();
};

#endif // RWLOCKGUARD_H
