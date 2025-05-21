#ifndef DNSRESOLVER_H
#define DNSRESOLVER_H

#include <netdb.h>
#include <arpa/inet.h>
#include <string>
#include <list>
#include <chrono>

#include "listener.h"
#include "fmqsockaddr.h"



/**
 * @brief The DnsResolver class does async DNS with getaddrinfo_a.
 *
 * Note that getaddrinfo_a uses threads. Had we known that, we would have probably rolled out our own. That
 * would also have prevented the fork problem:
 *
 * It turns out that getaddrinfo_a is not compatible with fork(). If it's been used once, the forked process can't
 * do DNS queries anymore. This is an issue for the test binary only at this point.
 */
class DnsResolver
{
    struct gaicb lookup;
    struct addrinfo request;
    std::string curName;
    std::chrono::time_point<std::chrono::steady_clock> getResultsTimeout;

    void freeStuff();
public:
    DnsResolver();
    DnsResolver(const DnsResolver &other) = delete;
    DnsResolver(DnsResolver &&other) = delete;
    DnsResolver &operator=(const DnsResolver &other) = delete;
    ~DnsResolver();

    void query(const std::string &text, ListenerProtocol protocol, std::chrono::milliseconds timeout);
    std::list<FMQSockaddr> getResult();
    bool idle() const;
};

#endif // DNSRESOLVER_H
