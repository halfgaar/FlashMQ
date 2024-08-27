#ifndef NOCOPY_H
#define NOCOPY_H

#include <optional>

template<typename T>
class NoCopy
{
    std::optional<T> data;

public:

    NoCopy<T>() = default;

    NoCopy<T>(const NoCopy<T> &other)
    {
        (void) other;
    }

    NoCopy<T>(NoCopy<T> &&other) = delete;

    NoCopy<T>& operator=(const NoCopy<T> &other)
    {
        (void)other;
        return *this;
    }

    NoCopy<T>& operator=(const T &other)
    {
        data = other;
        return *this;
    }

    operator bool() const
    {
        return data.operator bool();
    }

    const T& value() const
    {
        return data.value();
    }
};

#endif // NOCOPY_H
