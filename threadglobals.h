/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef THREADGLOBALS_H
#define THREADGLOBALS_H

#include "forward_declarations.h"

class Authentication;

class ThreadGlobals
{
    static thread_local Authentication *auth;
    static thread_local ThreadData *threadData;
    static thread_local Settings *settings;
public:
    static void assign(Authentication *auth);
    static Authentication *getAuth();

    static void assignThreadData(ThreadData *threadData);
    static ThreadData *getThreadData();

    static void assignSettings(Settings *settings);
    static Settings *getSettings();
};

#endif // THREADGLOBALS_H
