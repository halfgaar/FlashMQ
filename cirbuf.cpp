#include "cirbuf.h"

#include <time.h>

#include <iostream>
#include <exception>
#include <stdexcept>
#include <cassert>
#include <cstring>

#include "logger.h"
#include "utils.h"

CirBuf::CirBuf(size_t size) :
    size(size)
{
    if (size == 0)
        return;

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
    assert(n <= freeSpace());
    head = (head + n) & (size -1);
    assert(tail != head); // Putting things in the buffer must never end on tail, because tail == head == empty.
}

void CirBuf::advanceTail(uint32_t n)
{
    assert(n <= usedBytes());
    tail = (tail + n) & (size -1);
}

char CirBuf::peakAhead(uint32_t offset) const
{
    int b = buf[(tail + offset) & (size - 1)];
    return b;
}

void CirBuf::ensureFreeSpace(size_t n)
{
    const size_t _usedBytes = usedBytes();

    int mul = 1;

    while((mul * size - _usedBytes - 1) < n)
    {
        mul = mul << 1;
    }

    if (mul == 1)
        return;

    doubleSize(mul);
}

void CirBuf::doubleSize(uint factor)
{
    if (factor == 1)
        return;

    assert(isPowerOfTwo(factor));

    uint newSize = size * factor;
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

void CirBuf::reset()
{
    head = 0;
    tail = 0;

#ifndef NDEBUG
    memset(buf, 0, size);
#endif
}

// When you know the data you want to write fits in the buffer, use this.
void CirBuf::write(const void *buf, size_t count)
{
    assert(count <= freeSpace());

    ssize_t len_left = count;
    size_t src_i = 0;
    while (len_left > 0)
    {
        const size_t len = std::min<int>(len_left, maxWriteSize());
        assert(len > 0);
        const char *src = &reinterpret_cast<const char*>(buf)[src_i];
        std::memcpy(headPtr(), src, len);
        advanceHead(len);
        src_i += len;
        len_left -= len;
    }
    assert(len_left == 0);
    assert(src_i == count);
}

// When you know 'count' bytes are present and you want to read them into buf
void CirBuf::read(void *buf, const size_t count)
{
    assert(count <= usedBytes());

    char *_buf = static_cast<char*>(buf);
    int i = 0;
    ssize_t _packet_len = count;
    while (_packet_len > 0)
    {
        const int readlen = std::min<int>(maxReadSize(), _packet_len);
        assert(readlen > 0);
        std::memcpy(&_buf[i], tailPtr(), readlen);
        advanceTail(readlen);
        i += readlen;
        _packet_len -= readlen;
    }
    assert(_packet_len == 0);
    assert(i == _packet_len);
}
