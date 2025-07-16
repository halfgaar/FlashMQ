#ifndef FMQSSL_H
#define FMQSSL_H

#include <openssl/ssl.h>

#include "sslctxmanager.h"

class FmqSsl
{
    SSL* d = nullptr;

public:
    FmqSsl() = default;

    FmqSsl(const SslCtxManager &ssl_ctx);

    FmqSsl(const FmqSsl &other) = delete;

    FmqSsl(FmqSsl &&other);

    FmqSsl &operator=(const FmqSsl &other) = delete;

    FmqSsl &operator=(FmqSsl &&other);

    ~FmqSsl();

    operator bool() const
    {
        return d != nullptr;
    }
    SSL* get() const
    {
        return d;
    }

    void set_fd(int fd);
};

#endif // FMQSSL_H
