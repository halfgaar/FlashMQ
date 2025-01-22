/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "cirbuf.h"

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

    assert(isPowerOfTwo(size));

    buf = (char*)malloc(size);

    if (buf == NULL)
        throw std::runtime_error("Malloc error constructing buffer.");

#ifndef NDEBUG
    memset(buf, 0, size);
#endif
}

CirBuf::~CirBuf()
{
    free(buf);
    buf = nullptr;
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

    if (n > freeSpace())
        throw std::runtime_error("Trying to advance buffer head more then there bytes free are in the buffer.");

    head = (head + n) & (size -1);
    assert(tail != head); // Putting things in the buffer must never end on tail, because tail == head == empty.
}

void CirBuf::advanceTail(uint32_t n)
{
    assert(n <= usedBytes());

    if (n > usedBytes())
        throw std::runtime_error("Trying to advance buffer tail more then there bytes used are in the buffer.");

    tail = (tail + n) & (size -1);
}

char CirBuf::peakAhead(uint32_t offset) const
{
    assert(offset < usedBytes());

    if (offset >= usedBytes())
        throw std::runtime_error("Trying to peek more bytes then there are in the buffer.");

    char b = buf[(tail + offset) & (size - 1)];
    return b;
}

void CirBuf::ensureFreeSpace(const size_t n, const size_t max)
{
    if (n <= freeSpace())
        return;

    const size_t _usedBytes = usedBytes();

    size_t mul = 1;

    while((mul * size - _usedBytes - 1) < n && (mul*size) < max)
    {
        mul = mul << 1;
    }

    doubleSize(mul);
}

void CirBuf::doubleSize(uint factor)
{
    if (factor == 1)
        return;

    assert(isPowerOfTwo(factor));

    if ((static_cast<size_t>(size) * factor) > 2147483648)
        throw std::runtime_error("Trying to exceed circular buffer beyond its 2 GB limit.");

    uint32_t newSize = size * factor;
    char *newBuf = (char*)realloc(buf, newSize);

    if (newBuf == NULL)
        throw std::runtime_error("Malloc error doubling buffer size.");

#ifdef TESTING
    // I use this to detect the affected memory locations.
    memset(&newBuf[size], 68, newSize - size);
#endif

    uint32_t maxRead = maxReadSize();
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
    memset(&buf[head], 5, maxWriteSize());
#endif

    primedForSizeReset = false;
}

uint32_t CirBuf::getSize() const
{
    return size;
}

void CirBuf::resetSizeIfEligable(size_t size)
{
    // Ensuring the reset will only happen the second time the timer event hits.
    if (!primedForSizeReset)
    {
        primedForSizeReset = true;
        return;
    }

    if (usedBytes() > 0)
        return;

    resetSize(size);
}

void CirBuf::resetSize(size_t newSize)
{
    assert(usedBytes() == 0);
    primedForSizeReset = false;
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

void CirBuf::write(uint8_t b)
{
    ensureFreeSpace(1);
    buf[head] = b;
    advanceHead(1);
}

void CirBuf::write(uint8_t b, uint8_t b2)
{
    ensureFreeSpace(2);
    buf[head] = b;
    advanceHead(1);
    buf[head] = b2;
    advanceHead(1);
}

void CirBuf::write(const void *buf, size_t count)
{
    assert(size > 0);
    ensureFreeSpace(count);

    ssize_t len_left = count;
    size_t src_i = 0;
    while (len_left > 0)
    {
        const size_t len = std::min<ssize_t>(len_left, maxWriteSize());
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

std::vector<char> CirBuf::readToVector(const uint32_t max)
{
    assert(size > 0);

    uint32_t bytes_left = std::min<uint32_t>(max, usedBytes());
    std::vector<char> result(bytes_left);

    int guard = 0;
    auto pos = result.begin();
    while (bytes_left > 0 && guard++ < 10)
    {
        const uint32_t readlen = std::min<uint32_t>(maxReadSize(), bytes_left);
        assert(readlen <= maxReadSize());
        std::copy(tailPtr(), tailPtr() + readlen, pos);
        advanceTail(readlen);
        pos += readlen;
        bytes_left -= readlen;
    }
    assert(guard < 3);
    assert(bytes_left == 0);
    assert(pos == result.end());

    return result;
}

std::vector<char> CirBuf::readAllToVector()
{
    return readToVector(std::numeric_limits<uint32_t>::max());
}

/**
 * @brief CirBuf::operator == simplistic comparision. It doesn't take the fact that it's circular into account.
 * @param other
 * @return
 *
 * It was created for unit testing. read() and write() are non-const, so taking the circular properties into account
 * would need more/duplicate code that I don't need at this point.
 */
bool CirBuf::operator==(const CirBuf &other) const
{
#ifdef NDEBUG
    throw std::exception(); // you can't use it in release builds, because new buffers aren't zeroed.
#endif

    return tail == 0 && other.tail == 0
           && usedBytes() == other.usedBytes()
           && std::memcmp(buf, other.buf, size) == 0;
}
