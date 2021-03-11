#ifndef LISTENER_H
#define LISTENER_H

#include <string>
#include <memory>

#include "sslctxmanager.h"

enum class ListenerProtocol
{
    IPv46,
    IPv4,
    IPv6
};

struct Listener
{
    ListenerProtocol protocol = ListenerProtocol::IPv46;
    std::string inet4BindAddress;
    std::string inet6BindAddress;
    int port = 0;
    bool websocket = false;
    std::string sslFullchain;
    std::string sslPrivkey;
    std::unique_ptr<SslCtxManager> sslctx;

    void isValid();
    bool isSsl() const;
    std::string getProtocolName() const;
    void loadCertAndKeyFromConfig();

    std::string getBindAddress(ListenerProtocol p);
};
#endif // LISTENER_H
