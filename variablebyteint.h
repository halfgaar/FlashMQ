/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef VARIABLEBYTEINT_H
#define VARIABLEBYTEINT_H

#include <array>

#include "cirbuf.h"

class VariableByteInt
{
    std::array<char, 4> bytes;
    uint8_t len = 0;

public:
    VariableByteInt(uint32_t val);
    VariableByteInt() = default;

    void readIntoBuf(CirBuf &buf) const;
    VariableByteInt &operator=(uint32_t x);
    uint8_t getLen() const;
    const char *data() const;

    auto begin() const { return bytes.cbegin(); }
    auto end() const { return bytes.begin() + len; }
    size_t size() const { return len; }
};

#endif // VARIABLEBYTEINT_H
