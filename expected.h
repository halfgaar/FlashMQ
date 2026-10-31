#ifndef EXPECTED_H
#define EXPECTED_H

#include <variant>
#include <stdexcept>
#include <optional>

/**
 * @brief The Expected class has a value or an exception.
 *
 * C++ 23 has one built-in. We don't have c++ 23 yet. And, we can make a version that has no unchecked dereference.
 */
template <typename T>
class Expected
{
    std::variant<T, std::runtime_error> d;
public:
    Expected(T arg) :
        d(arg)
    {

    }

    Expected(const std::runtime_error &arg) :
        d(arg)
    {

    }

    explicit operator bool() const
    {
        return std::holds_alternative<T>(d);
    }

    const T &value()
    {
        if (std::holds_alternative<std::runtime_error>(d))
            throw std::get<std::runtime_error>(d);

        return std::get<T>(d);
    }

    const std::optional<T> optional_value()
    {
        if (!std::holds_alternative<T>(d))
            return std::optional<T>();

        return std::get<T>(d);
    }

    T &operator*()
    {
        if (std::holds_alternative<std::runtime_error>(d))
            throw std::get<std::runtime_error>(d);

        return std::get<T>(d);
    }

    T *operator->()
    {
        if (std::holds_alternative<std::runtime_error>(d))
            throw std::get<std::runtime_error>(d);

        return &std::get<T>(d);
    }
};

#endif // EXPECTED_H
