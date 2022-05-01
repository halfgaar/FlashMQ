#ifndef VARIABLEBYTEINT_H
#define VARIABLEBYTEINT_H

#include "cirbuf.h"

class VariableByteInt
{
    char bytes[4];
    uint8_t len = 0;

public:
    void readIntoBuf(CirBuf &buf) const;
    VariableByteInt &operator=(uint32_t x);
    uint8_t getLen() const;
    const char *data() const;
};

#endif // VARIABLEBYTEINT_H
