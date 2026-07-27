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

void GlobalStats::setExtra(const std::string &topic, const std::string &payload,  const bool volatile_message)
{
    auto locked_data = extras.lock();

    if (volatile_message)
        locked_data->extras_volatile[topic] = payload;
    else
        locked_data->extras_fixed[topic] = payload;
}

std::unordered_map<std::string, std::string> GlobalStats::getExtras()
{
    auto locked_data = extras.lock();
    std::unordered_map<std::string, std::string> result = locked_data->extras_fixed;
    result.insert(locked_data->extras_volatile.begin(), locked_data->extras_volatile.end());
    locked_data->extras_volatile.clear();
    return result;
}

