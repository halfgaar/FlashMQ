#ifndef X509MANAGER_H
#define X509MANAGER_H

#include <openssl/ssl.h>

class X509Manager
{
    X509 *d = nullptr;
public:
    X509Manager(const X509Manager &other) = delete;
    X509Manager(X509Manager &&other);
    X509Manager(const SSL *ssl);
    ~X509Manager();
    X509 *get();
    operator bool() const;

};

#endif // X509MANAGER_H
