#ifndef UTILS_H
#define UTILS_H

#include <string.h>
#include <errno.h>
#include <string>

template<typename T> int check(int rc)
{
    if (rc < 0)
    {
        char *err = strerror(errno);
        std::string msg(err);
        throw T(msg);
    }

    return rc;
}

#endif // UTILS_H
