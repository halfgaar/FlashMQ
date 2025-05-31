/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "globalstats.h"

GlobalStats::GlobalStats()
{

}

void GlobalStats::setExtra(const std::string &topic, const std::string &payload)
{
    auto locked_data = extras.lock();
    locked_data->operator[](topic) = payload;
}

std::unordered_map<std::string, std::string> GlobalStats::getExtras()
{
    auto locked_data = extras.lock();
    std::unordered_map<std::string, std::string> r = *locked_data;
    return r;
}

