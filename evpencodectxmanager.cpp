/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <stdexcept>

#include "evpencodectxmanager.h"

EvpEncodeCtxManager::EvpEncodeCtxManager()
{
    ctx = EVP_ENCODE_CTX_new();

    if (!ctx)
        throw std::runtime_error("Error allocating with EVP_ENCODE_CTX_new()");

    EVP_DecodeInit(ctx);
}

EvpEncodeCtxManager::~EvpEncodeCtxManager()
{
    EVP_ENCODE_CTX_free(ctx);
    ctx = nullptr;
}
