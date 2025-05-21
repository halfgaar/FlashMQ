#include "fmqsockaddr.h"

#include <cstring>
#include <stdexcept>

FMQSockaddr::FMQSockaddr(sockaddr *addr)
{
    std::memset(&val, 0, sizeof(struct sockaddr_in6));

    if (addr->sa_family == AF_INET)
    {
        std::memcpy(&val, addr, sizeof(struct sockaddr_in));
    }
    else if (addr->sa_family == AF_INET6)
    {
        std::memcpy(&val, addr, sizeof(struct sockaddr_in6));
    }
    else
    {
        throw std::runtime_error("Trying to make IPv4 or IPv6 address from address structure that is neither of those");
    }

    char buf[INET6_ADDRSTRLEN];
    memset(buf, 0, INET6_ADDRSTRLEN);

    void *myaddr = nullptr;

    if (val.sin6_family == AF_INET)
    {
        struct sockaddr_in *inaddr = reinterpret_cast<struct sockaddr_in*>(&val);
        myaddr = &inaddr->sin_addr;
    }
    else if (val.sin6_family == AF_INET6)
    {
        myaddr = &val.sin6_addr;
    }

    if (inet_ntop(val.sin6_family, myaddr, buf, INET6_ADDRSTRLEN) != nullptr)
    {
        this->text = std::string(buf);
    }
}

const sockaddr *FMQSockaddr::getSockaddr() const
{
    return reinterpret_cast<const struct sockaddr*>(&val);
}

socklen_t FMQSockaddr::getSize() const
{
    if (val.sin6_family == AF_INET)
        return sizeof(struct sockaddr_in);
    if (val.sin6_family == AF_INET6)
        return sizeof(struct sockaddr_in6);
    return 0;
}

void FMQSockaddr::setPort(uint16_t port)
{
    val.sin6_port = htons(port);
}

const std::string &FMQSockaddr::getText() const
{
    return text;
}

int FMQSockaddr::getFamily() const
{
    return val.sin6_family;
}
