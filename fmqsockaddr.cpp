#include "fmqsockaddr.h"

#include <cstring>
#include <stdexcept>
#include "utils.h"

FMQSockaddr::FMQSockaddr(const sockaddr *addr) :
    family(getFamilyFromSockAddr(addr))
{
    if (addr == nullptr)
        return;

    if (this->family == AF_INET)
    {
        std::memcpy(dat.data(), addr, sizeof(struct sockaddr_in));
    }
    else if (this->family == AF_INET6)
    {
        std::memcpy(dat.data(), addr, sizeof(struct sockaddr_in6));
    }
    else
    {
        throw std::runtime_error("Trying to make IPv4 or IPv6 address from address structure that is neither of those");
    }

    this->text = sockaddrToString(addr);
}

/**
 * This should (mostly) only be necessary when it's needed to pass to a system API. Don't be tempted to cast this back
 * into a specific sockaddr, because that will result in undefined behavior because of type aliasing
 * violations, unless your compiler bends the rules.
 */
const sockaddr *FMQSockaddr::getSockaddr() const
{
    return reinterpret_cast<const struct sockaddr*>(dat.data());
}

const char *FMQSockaddr::getData() const
{
    return dat.data();
}

socklen_t FMQSockaddr::getSize() const
{
    if (this->family == AF_INET)
        return sizeof(struct sockaddr_in);
    if (this->family == AF_INET6)
        return sizeof(struct sockaddr_in6);
    return 0;
}

void FMQSockaddr::setPort(uint16_t port)
{
    if (this->family == AF_INET)
    {
        struct sockaddr_in tmp;
        std::memcpy(&tmp, dat.data(), sizeof(tmp));
        tmp.sin_port = htons(port);
        std::memcpy(dat.data(), &tmp, sizeof(tmp));
    }
    else if (this->family == AF_INET6)
    {
        struct sockaddr_in6 tmp;
        std::memcpy(&tmp, dat.data(), sizeof(tmp));
        tmp.sin6_port = htons(port);
        std::memcpy(dat.data(), &tmp, sizeof(tmp));
    }
}

const std::string &FMQSockaddr::getText() const
{
    return text;
}

int FMQSockaddr::getFamily() const
{
    return this->family;
}
