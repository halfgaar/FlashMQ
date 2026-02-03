#ifndef CHECKEDUNIQUEPTR_H
#define CHECKEDUNIQUEPTR_H

#include <stdexcept>
#include <memory>

class CheckedUniquePtrNull : private std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return "CheckedUniquePtrNull pointer null";
    }
};

template<typename T>
class CheckedUniquePtr
{
    std::unique_ptr<T> d;

public:
    CheckedUniquePtr() = default;

    template<typename... Args>
    CheckedUniquePtr(Args... args) :
        d(args...)
    {

    }

    CheckedUniquePtr(std::unique_ptr<T> &&other)
    {
        d = std::move(other);
    }

    T &operator*()
    {
        if (!d)
            throw CheckedUniquePtrNull();

        return *d;
    }

    T *operator->()
    {
        if (!d)
            throw CheckedUniquePtrNull();

        return d.get();
    }

    void reset()
    {
        d.reset();
    }

    operator bool() const
    {
        return static_cast<bool>(d);
    }
};

#endif // CHECKEDUNIQUEPTR_H
