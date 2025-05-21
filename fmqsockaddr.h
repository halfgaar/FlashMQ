#ifndef FMQSOCKADDR_H
#define FMQSOCKADDR_H

#include <string>
#include <arpa/inet.h>

/**
 * @brief A class for storing an address up to in6 size. So, it's used for IPv4 and IPv6.
 */
class FMQSockaddr
{
    struct sockaddr_in6 val;
    std::string text;

public:
    FMQSockaddr(struct sockaddr *addr);
    const struct sockaddr *getSockaddr() const;
    socklen_t getSize() const;
    void setPort(uint16_t port);
    const std::string &getText() const;
    int getFamily() const;
};

#endif // FMQSOCKADDR_H
