/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <list>

#include "queuedtasks.h"
#include "logger.h"


bool QueuedTask::operator<(const QueuedTask &rhs) const
{
    return this->when < rhs.when;
}

QueuedTasks::QueuedTasks()
{

}

uint32_t QueuedTasks::addTask(std::function<void ()> f, uint32_t delayInMs, bool repeat)
{
    std::chrono::time_point<std::chrono::steady_clock> when = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayInMs);

    while(++nextId == 0 || tasks.find(nextId) != tasks.end()) { }

    const uint32_t id = nextId;
    std::shared_ptr<std::function<void()>> &inserted = tasks[id];
    inserted = std::make_shared<std::function<void()>>(std::move(f));

    QueuedTask t;
    t.id = id;
    t.f = inserted;
    t.when = when;
    t.interval = std::chrono::milliseconds(delayInMs);
    t.repeat = repeat;

    queuedTasks.insert(t);

    return id;
}

void QueuedTasks::eraseTask(uint32_t id)
{
    tasks.erase(id);
}

uint32_t QueuedTasks::getTimeTillNext() const
{
    if (__builtin_expect(queuedTasks.empty(), 1))
        return std::numeric_limits<uint32_t>::max();

    std::chrono::time_point<std::chrono::steady_clock> next = queuedTasks.begin()->when;
    std::chrono::milliseconds x = std::chrono::duration_cast<std::chrono::milliseconds>(next - std::chrono::steady_clock::now());
    std::chrono::milliseconds y = std::max<std::chrono::milliseconds>(std::chrono::milliseconds(0), x);
    return y.count();
}

void QueuedTasks::performAll()
{
    const auto now = std::chrono::steady_clock::now();

    std::vector<std::shared_ptr<std::function<void()>>> functions;

    for (auto pos = queuedTasks.begin(); pos != queuedTasks.end(); )
    {
        if (pos->when > now || functions.size() > 0xFFFF)
        {
            break;
        }

        const auto cur = pos++;

        const auto tpos = tasks.find(cur->id);
        if (tpos != tasks.end() && cur->f.lock() == tpos->second)
        {
            functions.push_back(tpos->second);

            if (cur->repeat)
            {
                QueuedTask requeue = *cur;
                requeue.when = std::chrono::steady_clock::now() + requeue.interval;
                queuedTasks.insert(requeue);
            }
            else
            {
                tasks.erase(tpos);
            }
        }

        queuedTasks.erase(cur);
    }

    for(const std::shared_ptr<std::function<void()>> &f : functions)
    {
        try
        {
            if (!f || !*f)
                continue;

            f->operator()();
        }
        catch (std::exception &ex)
        {
            Logger *logger = Logger::getInstance();
            logger->logf(LOG_ERR, "Error in delayed task: %s", ex.what());
        }
    }
}

void QueuedTasks::clear()
{
    queuedTasks.clear();
    tasks.clear();
}


