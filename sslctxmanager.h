#ifndef SSLCTXMANAGER_H
#define SSLCTXMANAGER_H

#include "openssl/ssl.h"

class SslCtxManager
{
    SSL_CTX *ssl_ctx = nullptr;
public:
    SslCtxManager();
    ~SslCtxManager();

    SSL_CTX *get() const;
};

#endif // SSLCTXMANAGER_H
