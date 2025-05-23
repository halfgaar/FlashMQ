#ifndef FMQSOCKADDR_H
#define FMQSOCKADDR_H

#include <string>
#include <arpa/inet.h>
#include <vector>
#include <optional>

/**
 * @brief A class for storing socket addresses, and accesses them in a way that avoids type aliasing violations.
 */
class FMQSockaddr
{
    std::vector<char> dat = std::vector<char>(sizeof(sockaddr_storage));
    sa_family_t family = AF_UNSPEC;
    std::string text;
    std::optional<std::string> name;

public:
    FMQSockaddr(const struct sockaddr *addr);
    const struct sockaddr *getSockaddr() const;
    const char *getData() const;
    socklen_t getSize() const;
    void setPort(uint16_t port);
    void setAddress(const std::string &address);
    void setAddressName(const std::string &addressName);
    const std::string &getText() const;
    int getFamily() const;
};

#endif // FMQSOCKADDR_H
