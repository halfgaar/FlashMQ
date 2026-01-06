#ifndef X509MANAGER_H
#define X509MANAGER_H

#include <memory>
#include <openssl/ssl.h>

class X509Manager
{
    std::unique_ptr<X509, void(*)(X509*)> d;
public:
    X509Manager(const SSL *ssl);
    X509 *get();
    operator bool() const;

};

#endif // X509MANAGER_H
