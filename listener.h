#ifndef LISTENER_H
#define LISTENER_H

#include <string>
#include <memory>

#include "sslctxmanager.h"

struct Listener
{
    int port = 0;
    bool websocket = false;
    std::string sslFullchain;
    std::string sslPrivkey;
    std::unique_ptr<SslCtxManager> sslctx;

    void isValid();
    bool isSsl() const;
    std::string getProtocolName() const;
    void loadCertAndKeyFromConfig();
};
#endif // LISTENER_H
