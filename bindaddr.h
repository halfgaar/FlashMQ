#ifndef BINDADDR_H
#define BINDADDR_H

#include <arpa/inet.h>
#include <memory>

/**
 * @brief The BindAddr struct helps creating the resource for bind(). It uses an intermediate struct sockaddr to avoid compiler warnings, and
 * this class helps a bit with resource management of it.
 */
struct BindAddr
{
    std::unique_ptr<sockaddr> p;
    socklen_t len = 0;
};

#endif // BINDADDR_H
