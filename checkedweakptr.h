#ifndef CHECKEDWEAKPTR_H
#define CHECKEDWEAKPTR_H

#include <memory>

class PointerNullException : private std::exception
{
public:
    virtual const char* what() const noexcept override
    {
        return "CheckedWeakPtr pointer null";
    }
};


template<typename T>
class CheckedWeakPtr
{
    std::weak_ptr<T> d;

public:
    CheckedWeakPtr() = default;

    CheckedWeakPtr(const std::shared_ptr<T> org) :
        d(org)
    {

    }

    CheckedWeakPtr<T> &operator=(const std::shared_ptr<T> &other)
    {
        d = other;
        return *this;
    }

    std::shared_ptr<T> lock()
    {
        std::shared_ptr<T> d2 = d.lock();

        if (!d2)
            throw PointerNullException();

        return d2;
    }

    bool expired() const
    {
        return d.expired();
    }
};

#endif // CHECKEDWEAKPTR_H
