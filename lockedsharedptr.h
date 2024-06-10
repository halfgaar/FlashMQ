#ifndef LOCKEDSHAREDPTR_H
#define LOCKEDSHAREDPTR_H

#include <mutex>
#include <memory>

template<typename T>
class LockedSharedPtr
{
    std::mutex m;
    std::shared_ptr<T> p;

public:

    LockedSharedPtr& operator=(const std::shared_ptr<T> &r) noexcept
    {
        std::lock_guard<std::mutex> l(m);
        p = r;
        return *this;
    }

    void reset()
    {
        std::lock_guard<std::mutex> l(m);
        p.reset();
    }

    std::shared_ptr<T> getCopy()
    {
        std::lock_guard<std::mutex> l(m);
        return p;
    }
};

#endif // LOCKEDSHAREDPTR_H
