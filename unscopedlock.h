#ifndef UNSCOPEDLOCK_H
#define UNSCOPEDLOCK_H

#include <mutex>

/**
 * @brief The UnscopedLock class is a simple variety of the std::lock_guard or std::scoped_lock that allows optional locking using RAII.
 *
 * STL doesn't provide a similar feature, or am I missing something? You could do it with smart pointers, but I want to avoid having to
 * use the free store.
 */
class UnscopedLock
{
    std::mutex &managedMutex;
    bool locked = false;

public:
    ~UnscopedLock();

    UnscopedLock(std::mutex &mutex);
    void lock();
};

#endif // UNSCOPEDLOCK_H
