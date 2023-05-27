/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef EVPENCODECTXMANAGER_H
#define EVPENCODECTXMANAGER_H

#include <openssl/evp.h>

struct EvpEncodeCtxManager
{
    EVP_ENCODE_CTX *ctx = nullptr;
    EvpEncodeCtxManager();
    ~EvpEncodeCtxManager();
};

#endif // EVPENCODECTXMANAGER_H
