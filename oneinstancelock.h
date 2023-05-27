/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef ONEINSTANCELOCK_H
#define ONEINSTANCELOCK_H

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "logger.h"

class OneInstanceLock
{
    int fd = -1;
    std::string lockFilePath;
    Logger *logger = Logger::getInstance();

public:
    OneInstanceLock();
    ~OneInstanceLock();
    void lock();
    void unlock();
};

#endif // ONEINSTANCELOCK_H
