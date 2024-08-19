/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "globalstats.h"

GlobalStats *GlobalStats::instance = nullptr;

GlobalStats::GlobalStats()
{

}

GlobalStats *GlobalStats::getInstance()
{
    if (GlobalStats::instance == nullptr)
        GlobalStats::instance = new GlobalStats();

    return GlobalStats::instance;
}

void GlobalStats::setExtra(const std::string &topic, const std::string &payload)
{
    std::lock_guard<std::mutex> locker(extras_mutex);

    extras[topic] = payload;
}

std::unordered_map<std::string, std::string> GlobalStats::getExtras()
{
    std::lock_guard<std::mutex> locker(extras_mutex);
    std::unordered_map<std::string, std::string> r = extras;
    return r;
}

