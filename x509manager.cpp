#include "x509manager.h"
#include <cassert>

X509Manager::X509Manager(const SSL *ssl)
{
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    this->d = SSL_get_peer_certificate(ssl);
#else
    this->d = SSL_get1_peer_certificate(ssl);
#endif
}

X509Manager::X509Manager(X509Manager &&other)
{
    assert(this != &other);

    X509_free(this->d);
    this->d = other.d;
    other.d = nullptr;
}

X509Manager::~X509Manager()
{
    X509_free(this->d);
    this->d = nullptr;
}

X509 *X509Manager::get()
{
    return this->d;
}

X509Manager::operator bool() const
{
    return d != nullptr;
}
