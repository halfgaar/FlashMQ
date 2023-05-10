/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
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
