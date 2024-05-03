/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef TIMER_H
#define TIMER_H

#include <functional>
#include <thread>
#include <list>

#include "logger.h"

#define MAX_TIMER_EVENTS 32

struct CallbackEntry
{
    std::chrono::time_point<std::chrono::steady_clock> lastExecuted = std::chrono::steady_clock::now();
    int64_t timeTillNext = 1000;
    uint64_t interval = 0;
    std::function<void ()> f = nullptr;
    std::string name;

    void updateExectedAt();
    void calculateNewWaitTime();
    bool operator <(const CallbackEntry &other) const;
};

// Simple timer that calls your callback. The callback is executed on the timer thread.
class Timer
{
    std::thread t;
    int epollfd = -1;
    int fd = -1;
    uint64_t sleeptime = 1000;
    int running = false;
    Logger *logger = Logger::getInstance();
    std::vector<CallbackEntry> callbacks;
    std::mutex callbacksMutex;

    void sortAndSetSleeptimeTillNext();
    void process();
    void wakeUpPoll();
public:
    Timer();
    ~Timer();
    void start();
    void stop();
    void addCallback(std::function<void()> f, uint64_t interval_ms, const std::string &name);
};

#endif // TIMER_H
