/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "sslctxmanager.h"
#include <stdexcept>

SslCtxManager::SslCtxManager() :
    ssl_ctx(SSL_CTX_new(TLS_server_method()))
{

}

SslCtxManager::SslCtxManager(const SSL_METHOD *method) :
    ssl_ctx(SSL_CTX_new(method))
{

}

SslCtxManager::~SslCtxManager()
{
    SSL_CTX_free(ssl_ctx);
    ssl_ctx = nullptr;
}

SSL_CTX *SslCtxManager::get() const
{
    return ssl_ctx;
}

SslCtxManager::operator bool() const
{
    return ssl_ctx == nullptr;
}

int SslCtxManager::tlsEnumToInt(TLSVersion v)
{
    switch (v)
    {
    case TLSVersion::TLSv1_0:
        return TLS1_VERSION;
    case TLSVersion::TLSv1_1:
        return TLS1_1_VERSION;
    case TLSVersion::TLSv1_2:
        return TLS1_2_VERSION;
    case TLSVersion::TLSv1_3:
        return TLS1_3_VERSION;
    default:
        throw std::runtime_error("Unsupported version in tlsEnumToInt");
    }
}

void SslCtxManager::setMinimumTlsVersion(TLSVersion min_version)
{
    if (!ssl_ctx)
        return;

    SSL_CTX_set_min_proto_version(ssl_ctx, tlsEnumToInt(min_version));
}
