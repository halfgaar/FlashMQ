#include "dnsresolver.h"

#include <unistd.h>
#include <cstring>
#include <signal.h>
#include <stdexcept>
#include <thread>
#include "utils.h"


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
    gai_cancel(&lookup);

    int n = 0;
    while (gai_error(&lookup) == EAI_INPROGRESS && n++ < 1000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

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

std::list<FMQSockaddr> DnsResolver::getResult()
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

    std::list<FMQSockaddr> results;
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
        FMQSockaddr result_sockaddr(cur_result->ai_addr);
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










