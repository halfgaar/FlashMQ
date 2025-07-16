#include <stdexcept>

#include <openssl/ssl.h>

#include "fmqssl.h"

FmqSsl::~FmqSsl()
{
    if (d == nullptr) return;

    /*
     * We write the shutdown when we can, but don't take error conditions into account. If socket buffers are full, because
     * clients disappear for instance, the socket is just closed. We don't care.
     *
     * Truncation attacks seem irrelevant. MQTT is frame based, so either end knows if the transmission is done or not. The
     * close_notify is not used in determining whether to use or discard the received data.
     */
    SSL_shutdown(d);

    SSL_free(d);
    d = nullptr;
}

FmqSsl::FmqSsl(const SslCtxManager &ssl_ctx) :
    d(SSL_new(ssl_ctx.get()))
{
}

FmqSsl::FmqSsl(FmqSsl &&other) :
    d(other.d)
{
    other.d = nullptr;
}

FmqSsl &FmqSsl::operator=(FmqSsl &&other)
{
    d = other.d;
    other.d = nullptr;
    return *this;
}

void FmqSsl::set_fd(int fd)
{
    if (!d) return;

    SSL_set_fd(d, fd);
}
