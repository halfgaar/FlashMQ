/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef GLOBALSTATS_H
#define GLOBALSTATS_H

#include <stdint.h>
#include <unordered_map>
#include <string>

#include "derivablecounter.h"

class GlobalStats
{
    static GlobalStats *instance;

    std::mutex extras_mutex;
    std::unordered_map<std::string, std::string> extras;

    GlobalStats();
public:
    static GlobalStats *getInstance();

    DerivableCounter socketConnects;

    void setExtra(const std::string &topic, const std::string &payload);
    const std::unordered_map<std::string, std::string> &getExtras() const;
};

#endif // GLOBALSTATS_H
