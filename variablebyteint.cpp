#include "variablebyteint.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

void VariableByteInt::readIntoBuf(CirBuf &buf) const
{
    assert(len > 0);
    buf.write(bytes, len);
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
