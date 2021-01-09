#include "cirbuf.h"

#include <time.h>

#include <iostream>
#include <exception>
#include <stdexcept>
#include <cassert>
#include <cstring>

#include "logger.h"

CirBuf::CirBuf(size_t size) :
    size(size)
{
    buf = (char*)malloc(size);

    if (buf == NULL)
        throw std::runtime_error("Malloc error constructing buffer.");

#ifndef NDEBUG
    memset(buf, 0, size);
#endif
}

CirBuf::~CirBuf()
{
    if (buf)
        free(buf);
}

uint32_t CirBuf::usedBytes() const
{
    int result = (head - tail) & (size-1);
    return result;
}

uint32_t CirBuf::freeSpace() const
{
    int result = (tail - (head + 1)) & (size-1);
    return result;
}

uint32_t CirBuf::maxWriteSize() const
{
    int end = size - 1 - head;
    int n = (end + tail) & (size-1);
    int result = n <= end ? n : end+1;
    return result;
}

uint32_t CirBuf::maxReadSize() const
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

void CirBuf::advanceHead(uint32_t n)
{
    head = (head + n) & (size -1);
    assert(tail != head); // Putting things in the buffer must never end on tail, because tail == head == empty.
}

void CirBuf::advanceTail(uint32_t n)
{
    tail = (tail + n) & (size -1);
}

char CirBuf::peakAhead(uint32_t offset) const
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

#ifndef NDEBUG
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_DEBUG, "New buf size: %d", size);
#endif

#ifdef TESTING
    memset(&buf[head], 5, maxWriteSize() + 2);
#endif

    resizedAt = time(NULL);
}

uint32_t CirBuf::getSize() const
{
    return size;
}

time_t CirBuf::bufferLastResizedSecondsAgo() const
{
    return time(NULL) - resizedAt;
}

void CirBuf::resetSize(size_t newSize)
{
    assert(usedBytes() == 0);
    if (this->size == newSize)
        return;
    char *newBuf = (char*)malloc(newSize);
    if (newBuf == NULL)
        throw std::runtime_error("Malloc error resizing buffer.");
    free(buf);
    buf = newBuf;
    this->size = newSize;
    head = 0;
    tail = 0;
    resizedAt = time(NULL);
#ifndef NDEBUG
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_DEBUG, "Reset buf size: %d", size);
    memset(buf, 0, newSize);
#endif
}
