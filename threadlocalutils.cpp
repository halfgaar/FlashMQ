/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifdef __SSE4_2__

#include "threadlocalutils.h"

#include <algorithm>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <stdexcept>

std::vector<std::string> SimdUtils::splitTopic(const std::string &topic)
{
    const unsigned s = topic.size();

    if (s > 65535)
        throw std::runtime_error("Trying to split a string longer than the maximum MQTT topic length.");

    // Prefill the last 16 byte "line" with zeros
    _mm_store_si128((__m128i *)(topicCopy.begin() + (s & ~15u)), _mm_setzero_si128());

    std::copy_n(topic.begin(), s, topicCopy.begin());
    /* Add a trailing '/'
     * The reason is that we then always find a last / so a special case to handle the last subtopic is not necessary.
     * We can just stop searching when the location is of this trailing /
     * */
    topicCopy[s] = '/';

    std::vector<std::string> output;
    output.reserve(16);

    const char * b = topicCopy.data();
    const char * i = topicCopy.data();
    const char * const e = topicCopy.data() + s;
    while (true)
    {
        __m128i loaded = _mm_loadu_si128((const __m128i *)i);
        unsigned index = _mm_cmpestri(slashes, 1, loaded, 16, 0);
        i += index;
        if (index < 16)
        {
            // This means that a '/' was found
            // i will point at the position where '/' was found
            output.emplace_back(b, i);
            if (i == e)
                break;
            ++i; // advance over the separator
            b = i;
        }
    }

    return output;
}

#endif
