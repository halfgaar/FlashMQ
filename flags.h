#ifndef FLAGS_H
#define FLAGS_H

#include <stdint.h>
#include <limits>

template<typename FlagType>
class Flags
{
    uint32_t flags = 0;

public:

    bool hasFlagSet(FlagType val) const
    {
        return static_cast<bool>(this->flags & (1 << static_cast<uint32_t>(val)));
    }

    bool hasNone() const
    {
        return flags == 0;
    }

    bool hasAll() const
    {
        return flags == std::numeric_limits<uint32_t>::max();
    }

    void setFlag(FlagType val)
    {
        flags |= (1 << static_cast<uint32_t>(val));
    }

    void clearFlag(FlagType val)
    {
        flags &= ~(1 << static_cast<uint32_t>(val));
    }

    void clearAll()
    {
        flags = 0;
    }

    void setAll()
    {
        flags = std::numeric_limits<uint32_t>::max();
    }
};

#endif // FLAGS_H
