#include "unscopedlock.h"

UnscopedLock::~UnscopedLock()
{
    if (locked)
    {
        managedMutex.unlock();
    }
}

UnscopedLock::UnscopedLock(std::mutex &mutex) :
    managedMutex(mutex)
{

}

void UnscopedLock::lock()
{
    managedMutex.lock();
    locked = true;
}
