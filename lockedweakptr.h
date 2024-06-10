#ifndef LOCKEDWEAKPTR_H
#define LOCKEDWEAKPTR_H

#include <mutex>
#include <memory>

template<typename T>
class LockedWeakPtr
{
    std::mutex m;
    std::weak_ptr<T> p;

public:

    std::shared_ptr<T> lock()
    {
        std::lock_guard<std::mutex> l(m);
        return p.lock();
    }

    bool expired()
    {
        std::lock_guard<std::mutex> l(m);
        return p.expired();
    }

    LockedWeakPtr& operator=(const std::shared_ptr<T> &r) noexcept
    {
        std::lock_guard<std::mutex> l(m);
        p = r;
        return *this;
    }
};

#endif // LOCKEDWEAKPTR_H
