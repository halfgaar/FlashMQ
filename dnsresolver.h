#ifndef DNSRESOLVER_H
#define DNSRESOLVER_H

#include <netdb.h>
#include <arpa/inet.h>
#include <string>
#include <list>
#include <chrono>

#include "listener.h"

/**
 * @brief A class for storing an address up to in6 size. So, it's used for IPv4 and IPv6.
 */
class FMQSockaddr_in6
{
    struct sockaddr_in6 val;
    std::string text;

public:
    FMQSockaddr_in6(struct sockaddr *addr);
    const struct sockaddr *getSockaddr() const;
    socklen_t getSize() const;
    void setPort(uint16_t port);
    const std::string &getText() const;
    int getFamily() const;
};

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
    std::list<FMQSockaddr_in6> getResult();
    bool idle() const;
};

#endif // DNSRESOLVER_H
