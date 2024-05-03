/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "timer.h"
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <inttypes.h>

#include "utils.h"

void CallbackEntry::updateExectedAt()
{
    this->lastExecuted = std::chrono::steady_clock::now();
}

void CallbackEntry::calculateNewWaitTime()
{
    const std::chrono::milliseconds elapsedSinceLastCall = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastExecuted);
    int64_t newDelay = this->interval - elapsedSinceLastCall.count();
    if (newDelay < 0)
        newDelay = 0;
    this->timeTillNext = newDelay;
}

bool CallbackEntry::operator <(const CallbackEntry &other) const
{
    return this->timeTillNext < other.timeTillNext;
}

Timer::Timer()
{
    fd = eventfd(0, EFD_NONBLOCK);
    epollfd = check<std::runtime_error>(epoll_create(999));

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev));
}

Timer::~Timer()
{
    if (fd >= 0)
        close(fd);

    if (epollfd >= 0)
        close(epollfd);
}

void Timer::start()
{
    running = true;

    auto f = std::bind(&Timer::process, this);
    this->t = std::thread(f, this);

    pthread_t native = this->t.native_handle();
    pthread_setname_np(native, "Timer");
}

void Timer::stop()
{
    running = false;
    uint64_t one = 1;
    check<std::runtime_error>(write(fd, &one, sizeof(uint64_t)));
    t.join();
}

void Timer::addCallback(std::function<void ()> f, uint64_t interval_ms, const std::string &name)
{
    logger->log(LOG_DEBUG) << "Adding event '" << name << "' to the timer with an interval of " << interval_ms << " ms.";

    std::lock_guard<std::mutex> locker(this->callbacksMutex);

    CallbackEntry c;
    c.f = f;
    c.interval = interval_ms;
    c.name = name;
    callbacks.push_back(std::move(c));
    sortAndSetSleeptimeTillNext();
    wakeUpPoll();
}

void Timer::sortAndSetSleeptimeTillNext()
{
    for(CallbackEntry &c : callbacks)
    {
        c.calculateNewWaitTime();
    }

    std::sort(callbacks.begin(), callbacks.end());
    this->sleeptime = callbacks.front().timeTillNext;
}

void Timer::process()
{
    maskAllSignalsCurrentThread();

    struct epoll_event events[MAX_TIMER_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_TIMER_EVENTS);

    while (running)
    {
        //logger->logf(LOG_DEBUG, "Timer sleeping for %d ms until event '%s' or callbacks are added.", sleeptime, callbacks.front().name.c_str());
        int num_fds = epoll_wait(this->epollfd, events, MAX_TIMER_EVENTS, sleeptime);

        if (!running)
            continue;

        if (num_fds < 0)
        {
            if (errno == EINTR)
                continue;
            logger->logf(LOG_ERR, "Waiting for timer fd error: %s", strerror(errno));
        }

        // If it was the eventfd, an action woke up the loop, and not a pending event.
        for (int i = 0; i < num_fds; i++)
        {
            int cur_fd = events[i].data.fd;

            if (cur_fd == this->fd)
            {
                uint64_t eventfd_value = 0;
                check<std::runtime_error>(read(fd, &eventfd_value, sizeof(uint64_t)));
            }

            continue;
        }

        std::lock_guard<std::mutex> locker(this->callbacksMutex);

        logger->logf(LOG_DEBUG, "Calling timed event '%s'.", callbacks.front().name.c_str());

        CallbackEntry &c = callbacks.front();
        c.updateExectedAt();

        try
        {
            c.f();
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Error in timer task: %s", ex.what());
        }

        sortAndSetSleeptimeTillNext();
    }
}

void Timer::wakeUpPoll()
{
    if (!running)
        return;

    uint64_t one = 1;
    check<std::runtime_error>(write(fd, &one, sizeof(uint64_t)));
}

