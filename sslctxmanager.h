/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef SSLCTXMANAGER_H
#define SSLCTXMANAGER_H

#include <memory>
#include <openssl/ssl.h>
#include "enums.h"

class SslCtxManager
{
    std::unique_ptr<SSL_CTX, void(*)(SSL_CTX*)> ssl_ctx;
public:
    SslCtxManager();
    SslCtxManager(const SSL_METHOD *method);

    SSL_CTX *get() const;

    static int tlsEnumToInt(TLSVersion v);
    void setMinimumTlsVersion(TLSVersion min_version);
};

#endif // SSLCTXMANAGER_H
