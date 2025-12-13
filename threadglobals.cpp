/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "threadglobals.h"
#include <cassert>

thread_local CheckedSharedPtr<ThreadData> ThreadGlobals::threadData;
thread_local Settings *ThreadGlobals::settings = nullptr;

void ThreadGlobals::assignThreadData(const std::shared_ptr<ThreadData> &threadData)
{
#ifndef TESTING
    assert(!static_cast<bool>(ThreadGlobals::threadData));
#endif
    ThreadGlobals::threadData = threadData;
}

const CheckedSharedPtr<ThreadData> &ThreadGlobals::getThreadData()
{
    return threadData;
}

void ThreadGlobals::assignSettings(Settings *settings)
{
#ifndef TESTING
    assert(ThreadGlobals::settings == nullptr || ThreadGlobals::settings == settings);
#endif
    ThreadGlobals::settings = settings;
}

Settings *ThreadGlobals::getSettings()
{
    return settings;
}

