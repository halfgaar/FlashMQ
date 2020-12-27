#include "cirbuf.h"

#include <iostream>
#include <exception>
#include <stdexcept>
#include <cassert>
#include <cstring>

CirBuf::CirBuf(size_t size) :
    size(size)
{
    buf = (char*)malloc(size);

    if (buf == NULL)
        throw std::runtime_error("Malloc error constructing client.");

#ifndef NDEBUG
    memset(buf, 0, size);
#endif
}

CirBuf::~CirBuf()
{
    if (buf)
        free(buf);
}

uint CirBuf::usedBytes() const
{
    int result = (head - tail) & (size-1);
    return result;
}

uint CirBuf::freeSpace() const
{
    int result = (tail - (head + 1)) & (size-1);
    return result;
}

int CirBuf::maxWriteSize() const
{
    int end = size - 1 - head;
    int n = (end + tail) & (size-1);
    int result = n <= end ? n : end+1;
    return result;
}

int CirBuf::maxReadSize() const
{
    int end = size - tail;
    int n = (head + end) & (size-1);
    int result = n < end ? n : end;
    return result;
}

char *CirBuf::headPtr()
{
    return &buf[head];
}

char *CirBuf::tailPtr()
{
    return &buf[tail];
}

void CirBuf::advanceHead(int n)
{
    head = (head + n) & (size -1);
    assert(tail != head); // Putting things in the buffer must never end on tail, because tail == head == empty.
}

void CirBuf::advanceTail(int n)
{
    tail = (tail + n) & (size -1);
}

int CirBuf::peakAhead(int offset) const
{
    int b = buf[(tail + offset) & (size - 1)];
    return b;
}

void CirBuf::doubleSize()
{
    uint newSize = size * 2;
    char *newBuf = (char*)realloc(buf, newSize);

    if (newBuf == NULL)
        throw std::runtime_error("Malloc error doubling buffer size.");

    uint maxRead = maxReadSize();
    buf = newBuf;

    if (head < tail)
    {
        std::memcpy(&buf[tail + maxRead], buf, head);
    }

    head = tail + usedBytes();
    size = newSize;

    std::cout << "New buf size: " << size << std::endl;

#ifdef TESTING
    memset(&buf[head], 5, maxWriteSize() + 2);
#endif
}

uint CirBuf::getSize() const
{
    return size;
}
