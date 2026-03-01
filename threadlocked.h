#ifndef THREADLOCKED_H
#define THREADLOCKED_H

#include <thread>
#include <optional>
#include <cassert>

template<typename T>
class ThreadLocked
{
    T d;
#ifndef NDEBUG
    std::optional<std::thread::id> user;
#endif

public:
    ThreadLocked() = default;

    template<typename... Args>
    ThreadLocked(Args... args) :
        d(args...),
        user(std::this_thread::get_id())
    {

    }

    ThreadLocked<T> &operator=(ThreadLocked<T> &&other)
    {
#ifndef NDEBUG
        assert(!user || user.value() == std::this_thread::get_id());
#endif
        d = std::forward(other.d);
        return *this;
    }

    ThreadLocked<T> &operator=(T &&other)
    {
#ifndef NDEBUG
        assert(!user || user.value() == std::this_thread::get_id());
#endif
        d = std::forward<T>(other);
        return *this;
    }

    T &operator*() noexcept
    {
#ifndef NDEBUG
        if (!user)
            user = std::this_thread::get_id();
        assert(user.value() == std::this_thread::get_id());
#endif
        return d;
    }

    T *operator->() noexcept
    {
#ifndef NDEBUG
        if (!user)
            user = std::this_thread::get_id();
        assert(user.value() == std::this_thread::get_id());
#endif
        return &d;
    }

    void reset_thread()
    {
#ifndef NDEBUG
        user.reset();
#endif
    }
};

#endif // THREADLOCKED_H
