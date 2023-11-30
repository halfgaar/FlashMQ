/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "derivablecounter.h"

void DerivableCounter::inc(uint64_t n)
{
    val += n;
}

uint64_t DerivableCounter::get() const
{
    return val;
}

/**
 * @brief DerivableCounter::getPerSecond returns the amount per second since last time this method was called.
 * @return
 *
 * Even though the class it not meant to be thread-safe, this method does use a mutex, because obtaining the value can be
 * scheduled in different threads.
 */
double DerivableCounter::getPerSecond()
{
    std::lock_guard<std::mutex> locker(timeMutex);

    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    std::chrono::milliseconds msSinceLastTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - timeOfPrevious);
    uint64_t messagesTimes1000 = (val - valPrevious) * 1000;
    double result = messagesTimes1000 / static_cast<double>(msSinceLastTime.count() + 1); // branchless avoidance of div by 0;
    timeOfPrevious = now;
    valPrevious = val;
    return result;
}
