#ifndef CIRBUF_H
#define CIRBUF_H

#include <stddef.h>
#include <stdlib.h>

// Optimized circular buffer, works only with sizes power of two.
class CirBuf
{
#ifdef TESTING
    friend class MainTests;
#endif

    char *buf = NULL;
    uint head = 0;
    uint tail = 0;
    uint size = 0;
public:

    CirBuf(size_t size);
    ~CirBuf();

    uint usedBytes() const;
    uint freeSpace() const;
    int maxWriteSize() const;
    int maxReadSize() const;
    char *headPtr();
    char *tailPtr();
    void advanceHead(int n);
    void advanceTail(int n);
    int peakAhead(int offset) const;
    void doubleSize();
    uint getSize() const;
};

#endif // CIRBUF_H
