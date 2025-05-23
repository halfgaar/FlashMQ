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

void FMQSockaddr::setAddress(const std::string &address)
{
    in_port_t port = 0;

    if (this->family == AF_INET)
    {
        struct sockaddr_in tmp;
        std::memcpy(&tmp, dat.data(), sizeof(tmp));
        port = tmp.sin_port;
    }
    else if (this->family == AF_INET6)
    {
        struct sockaddr_in6 tmp;
        std::memcpy(&tmp, dat.data(), sizeof(tmp));
        port = tmp.sin6_port;
    }

    {
        struct in_addr a;
        std::memset(&a, 0, sizeof(a));
        if (inet_pton(AF_INET, address.c_str(), &a) > 0)
        {
            sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));

            addr.sin_addr = a;
            addr.sin_port = port;
            addr.sin_family = AF_INET;

            memcpy(dat.data(), &addr, sizeof(addr));
            text = sockaddrToString(getSockaddr());

            return;
        }
    }

    {
        struct in6_addr a;
        std::memset(&a, 0, sizeof(a));
        if (inet_pton(AF_INET6, address.c_str(), &a) > 0)
        {
            sockaddr_in6 addr;
            std::memset(&addr, 0, sizeof(addr));

            addr.sin6_addr = a;
            addr.sin6_port = port;
            addr.sin6_family = AF_INET6;

            memcpy(dat.data(), &addr, sizeof(addr));
            text = sockaddrToString(getSockaddr());
        }
    }
}

void FMQSockaddr::setAddressName(const std::string &addressName)
{
    this->name = addressName;
}

const std::string &FMQSockaddr::getText() const
{
    if (name)
        return name.value();

    return text;
}

int FMQSockaddr::getFamily() const
{
    return this->family;
}
