/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef DERIVABLECOUNTER_H
#define DERIVABLECOUNTER_H

#include <chrono>
#include <mutex>

/**
 * @brief The DerivableCounter is a counter which can derive val/dt.
 *
 * It's not thread-safe, to avoid unnecessary locking. You should have counters per thread.
 */
class DerivableCounter
{
    uint64_t val = 0;
    uint64_t valPrevious = 0;
    std::chrono::time_point<std::chrono::steady_clock> timeOfPrevious = std::chrono::steady_clock::now();
    std::mutex timeMutex;

public:

    void inc(uint64_t n = 1);
    uint64_t get() const;
    uint64_t getPerSecond();
};

#endif // DERIVABLECOUNTER_H
