#ifndef MUTEXOWNED_H
#define MUTEXOWNED_H

#include <mutex>
#include <memory>

class MutexOwnedObjectNull : private std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return "MutexOwnedObjectNull";
    }
};

template<typename T>
class MutexLocked
{
    std::unique_lock<std::mutex> l;
    T *d = nullptr;

public:

    MutexLocked() = default;

    MutexLocked(T &other, std::mutex &m) :
        l(m),
        d(&other)
    {

    }

    MutexLocked(T &other, std::mutex &m, std::try_to_lock_t try_to_lock) :
        l(m, try_to_lock),
        d(&other)
    {

    }

    MutexLocked(T &other, std::mutex &m, std::defer_lock_t defer_lock) :
        l(m, defer_lock),
        d(&other)
    {

    }

    MutexLocked(const MutexLocked<T> &other) = delete;

    MutexLocked<T> &operator=(const MutexLocked<T> &other) = delete;

    MutexLocked<T> &operator=(MutexLocked<T> &&other) noexcept
    {
        l = std::move(other.l);
        d = other.d;
        other.d = nullptr;

        return *this;
    }

    MutexLocked(MutexLocked<T> &&other) noexcept :
        l(std::move(other.l)),
        d(other.d)
    {
        other.d = nullptr;
    }

    ~MutexLocked()
    {
        d = nullptr;
    }

    T &operator*()
    {
        if (!d)
            throw MutexOwnedObjectNull();

        return *d;
    }

    T *operator->()
    {
        if (!d)
            throw MutexOwnedObjectNull();

        return d;
    }

    void reset()
    {
        d = nullptr;
        if (l.owns_lock())
            l.unlock();
    }

    const std::unique_lock<std::mutex> &get_lock()
    {
        return l;
    }
};

template<typename T>
class MutexOwned
{
    std::mutex m;
    T d;

public:
    template<typename... Args>
    MutexOwned(Args... args) :
        d(args...)
    {

    }

    MutexLocked<T> lock()
    {
        MutexLocked<T> r(d, m);
        return r;
    }

    MutexLocked<T> lock(std::try_to_lock_t try_to_lock)
    {
        MutexLocked<T> r(d, m, try_to_lock);
        return r;
    }

    MutexLocked<T> lock(std::defer_lock_t defer_lock)
    {
        MutexLocked<T> r(d, m, defer_lock);
        return r;
    }
};

#endif // MUTEXOWNED_H
