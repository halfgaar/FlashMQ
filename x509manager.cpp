#include "x509manager.h"
#include <cassert>

X509Manager::X509Manager(const SSL *ssl) :
    d(nullptr, X509_free)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    this->d.reset(SSL_get_peer_certificate(ssl));
#else
    this->d.reset(SSL_get1_peer_certificate(ssl));
#endif
}

X509 *X509Manager::get()
{
    return this->d.get();
}

X509Manager::operator bool() const
{
    return d != nullptr;
}
