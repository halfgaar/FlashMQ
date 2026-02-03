#ifndef SHAREDMUTEXOWNED_H
#define SHAREDMUTEXOWNED_H

#include <mutex>
#include <shared_mutex>

class SharedMutexOwnedObjectNull : private std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return "SharedMutexOwnedObjectNull";
    }
};

class SharedMutexOwnedUnlockedAccess : private std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return "SharedMutexOwnedUnlockedAccess";
    }
};

template<typename T, typename LockType>
class SharedMutexLocked
{
    LockType l;
    T *d = nullptr;

public:

    SharedMutexLocked() = default;

    SharedMutexLocked(T &other, std::shared_mutex &m) :
        l(m),
        d(&other)
    {

    }

    SharedMutexLocked(T &other, std::shared_mutex &m, std::try_to_lock_t try_to_lock) :
        l(m, try_to_lock),
        d(&other)
    {

    }

    SharedMutexLocked(T &other, std::shared_mutex &m, std::defer_lock_t defer_lock) :
        l(m, defer_lock),
        d(&other)
    {

    }

    SharedMutexLocked(const SharedMutexLocked<T, LockType> &other) = delete;
    SharedMutexLocked<T, LockType> &operator=(const SharedMutexLocked<T, LockType> &other) = delete;

    SharedMutexLocked<T, LockType> &operator=(SharedMutexLocked<T, LockType> &&other) noexcept
    {
        l = std::move(other.l);
        d = other.d;
        other.d = nullptr;

        return *this;
    }

    SharedMutexLocked(SharedMutexLocked<T, LockType> &&other) noexcept :
        l(std::move(other.l)),
        d(other.d)
    {
        other.d = nullptr;
    }

    ~SharedMutexLocked()
    {
        d = nullptr;
    }

    T &operator*()
    {
        if (!d)
            throw SharedMutexOwnedObjectNull();

        if (!l.owns_lock())
            throw SharedMutexOwnedUnlockedAccess();

        return *d;
    }

    T *operator->()
    {
        if (!d)
            throw SharedMutexOwnedObjectNull();

        if (!l.owns_lock())
            throw SharedMutexOwnedUnlockedAccess();

        return d;
    }

    void reset()
    {
        d = nullptr;
        if (l.owns_lock())
            l.unlock();
        l.release();
    }

    void lock()
    {
        l.lock();
    }

    bool owns_lock() const
    {
        return l.owns_lock();
    }
};

template<typename T>
class SharedMutexOwned
{
    std::shared_mutex m;
    T d;

public:
    template<typename... Args>
    SharedMutexOwned(Args... args) :
        d(args...)
    {

    }

    SharedMutexLocked<T, std::unique_lock<std::shared_mutex>> unique_lock()
    {
        SharedMutexLocked<T,  std::unique_lock<std::shared_mutex>> r(d, m);
        return r;
    }

    SharedMutexLocked<T, std::unique_lock<std::shared_mutex>> unique_lock(std::try_to_lock_t try_to_lock)
    {
        SharedMutexLocked<T,  std::unique_lock<std::shared_mutex>> r(d, m, try_to_lock);
        return r;
    }

    SharedMutexLocked<T, std::unique_lock<std::shared_mutex>> unique_lock(std::defer_lock_t defer_lock)
    {
        SharedMutexLocked<T,  std::unique_lock<std::shared_mutex>> r(d, m, defer_lock);
        return r;
    }

    SharedMutexLocked<const T, std::shared_lock<std::shared_mutex>> shared_lock()
    {
        SharedMutexLocked<const T,  std::shared_lock<std::shared_mutex>> r(d, m);
        return r;
    }

    SharedMutexLocked<const T, std::shared_lock<std::shared_mutex>> shared_lock(std::try_to_lock_t try_to_lock)
    {
        SharedMutexLocked<const T,  std::shared_lock<std::shared_mutex>> r(d, m, try_to_lock);
        return r;
    }

    SharedMutexLocked<const T, std::shared_lock<std::shared_mutex>> shared_lock(std::defer_lock_t defer_lock)
    {
        SharedMutexLocked<const T,  std::shared_lock<std::shared_mutex>> r(d, m, defer_lock);
        return r;
    }
};


#endif // SHAREDMUTEXOWNED_H
