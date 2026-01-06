#ifndef FMQSSL_H
#define FMQSSL_H

#include <memory>
#include <openssl/ssl.h>

#include "sslctxmanager.h"

class FmqSsl
{
    std::unique_ptr<SSL, void(*)(SSL*)> d;

public:
    FmqSsl();
    FmqSsl(const SslCtxManager &ssl_ctx);
    FmqSsl(FmqSsl &&) = default;
    ~FmqSsl();

    FmqSsl& operator=(FmqSsl&&) = default;

    operator bool() const
    {
        return d != nullptr;
    }

    SSL* get() const
    {
        return d.get();
    }

    void set_fd(int fd);
};

#endif // FMQSSL_H
