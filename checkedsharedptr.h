#ifndef CHECKEDSHAREDPTR_H
#define CHECKEDSHAREDPTR_H

#include <memory>
#include <cassert>
#include <stdexcept>

template<typename T>
class CheckedSharedPtr
{
    std::shared_ptr<T> d;

public:
    CheckedSharedPtr() = default;

    CheckedSharedPtr(const std::shared_ptr<T> &org) :
        d(org)
    {

    }

    CheckedSharedPtr<T> &operator=(const std::shared_ptr<T> &other)
    {
        d = other;
        return *this;
    }

    T &operator*() const
    {
        assert(d);
        if (!d)
            throw std::runtime_error("CheckedSharedPtr is null");

        return *d;
    }

    T *operator->() const
    {
        assert(d);
        if (!d)
            throw std::runtime_error("CheckedSharedPtr is null");

        return d.get();
    }

    const std::shared_ptr<T> &get() const
    {
        return d;
    }

    operator bool() const
    {
        return static_cast<bool>(d);
    }

    void reset()
    {
        d.reset();
    }

};

#endif // CHECKEDSHAREDPTR_H
