#include "dnsresolver.h"

#include <unistd.h>
#include <cstring>
#include <signal.h>
#include <stdexcept>
#include "utils.h"

FMQSockaddr_in6::FMQSockaddr_in6(sockaddr *addr)
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

const sockaddr *FMQSockaddr_in6::getSockaddr() const
{
    return reinterpret_cast<const struct sockaddr*>(&val);
}

socklen_t FMQSockaddr_in6::getSize() const
{
    if (val.sin6_family == AF_INET)
        return sizeof(struct sockaddr_in);
    if (val.sin6_family == AF_INET6)
        return sizeof(struct sockaddr_in6);
    return 0;
}

void FMQSockaddr_in6::setPort(uint16_t port)
{
    val.sin6_port = htons(port);
}

const std::string &FMQSockaddr_in6::getText() const
{
    return text;
}

int FMQSockaddr_in6::getFamily() const
{
    return val.sin6_family;
}


void DnsResolver::freeStuff()
{
    curName.clear();
    lookup.ar_name = nullptr;
    freeaddrinfo(lookup.ar_result);
    lookup.ar_result = nullptr;
}

DnsResolver::DnsResolver()
{
    std::memset(&lookup, 0, sizeof(struct gaicb));
    std::memset(&request, 0, sizeof(struct addrinfo));

    request.ai_family = AF_UNSPEC;
    request.ai_socktype = SOCK_STREAM;
    request.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;

    lookup.ar_request = &request;
}

DnsResolver::~DnsResolver()
{
    freeStuff();
}

void DnsResolver::query(const std::string &text, ListenerProtocol protocol, std::chrono::milliseconds timeout)
{
    gai_cancel(&lookup);
    freeStuff();

    curName = text;
    getResultsTimeout = std::chrono::steady_clock::now() + timeout;

    lookup.ar_name = curName.c_str();
    lookup.ar_service = nullptr;

    request.ai_family = AF_UNSPEC;
    if (protocol == ListenerProtocol::IPv4)
        request.ai_family = AF_INET;
    else if (protocol == ListenerProtocol::IPv6)
        request.ai_family = AF_INET6;

    struct gaicb *lookups[1];
    lookups[0] = &lookup;

    int err = 0;
    if ((err = getaddrinfo_a(GAI_NOWAIT, lookups, 1, nullptr)) != 0)
    {
        const std::string errorstr(gai_strerror(err));
        throw std::runtime_error(formatString("Dns lookup failure: %s", errorstr.c_str()));
    }
}

std::list<FMQSockaddr_in6> DnsResolver::getResult()
{
    if (curName.empty() || lookup.ar_name == nullptr)
    {
        throw std::runtime_error("No DNS query in progress");
    }

    if (std::chrono::steady_clock::now() > getResultsTimeout)
    {
        const std::string name = this->curName;
        freeStuff();
        throw std::runtime_error(formatString("DNS query for '%s' timed out.", name.c_str()));
    }

    std::list<FMQSockaddr_in6> results;
    int err = gai_error(&lookup);

    if (err == EAI_INPROGRESS)
        return results;

    if (err != 0)
    {
        const std::string errorstr(gai_strerror(err));
        freeStuff();
        throw std::runtime_error(formatString("Dns lookup failure: %s", errorstr.c_str()));
    }

    struct addrinfo *cur_result = lookup.ar_result;

    while (cur_result)
    {
        FMQSockaddr_in6 result_sockaddr(cur_result->ai_addr);
        results.push_back(result_sockaddr);
        cur_result = cur_result->ai_next;
    }

    freeStuff();

    if (results.empty())
    {
        throw std::runtime_error("No error received but also no DNS result?");
    }

    return results;
}

bool DnsResolver::idle() const
{
    return (curName.empty() || lookup.ar_name == nullptr);
}










