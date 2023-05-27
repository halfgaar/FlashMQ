/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "sslctxmanager.h"

SslCtxManager::SslCtxManager() :
    ssl_ctx(SSL_CTX_new(TLS_server_method()))
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
