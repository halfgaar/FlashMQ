/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "variablebyteint.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

VariableByteInt::VariableByteInt(uint32_t val)
{
    *this = val;
}

void VariableByteInt::readIntoBuf(CirBuf &buf) const
{
    assert(len > 0);
    buf.writerange(bytes.begin(), bytes.begin() + len);
}

VariableByteInt &VariableByteInt::operator=(uint32_t x)
{
    if (x > 268435455)
        throw std::runtime_error("Value of variable byte int to encode too big. Bug or corrupt packet?");

    len = 0;

    do
    {
        uint8_t encodedByte = x % 128;
        x = x / 128;
        if (x > 0)
            encodedByte = encodedByte | 128;
        bytes[len++] = encodedByte;
    }
    while(x > 0);

    return *this;
}

uint8_t VariableByteInt::getLen() const
{
    return len;
}

const char *VariableByteInt::data() const
{
    return &bytes[0];
}
