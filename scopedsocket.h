#ifndef SCOPEDSOCKET_H
#define SCOPEDSOCKET_H

#include <fcntl.h>
#include <unistd.h>

/**
 * @brief The ScopedSocket struct allows for a bit of RAII and move semantics on a socket fd.
 */
struct ScopedSocket
{
    int socket = 0;
    ScopedSocket(int socket);
    ScopedSocket(ScopedSocket &&other);
    ~ScopedSocket();
};

#endif // SCOPEDSOCKET_H
