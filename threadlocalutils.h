/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef THREADLOCALUTILS_H
#define THREADLOCALUTILS_H

#ifdef __SSE4_2__

#include <vector>
#include <array>
#include <string>
#include <immintrin.h>

#define TOPIC_MEMORY_LENGTH 65560



class SimdUtils
{
    alignas(64) std::array<char, TOPIC_MEMORY_LENGTH> topicCopy;
    __m128i slashes = _mm_set1_epi8('/');
    __m128i lowerBound = _mm_set1_epi8(0x20);
    __m128i lastAsciiChar = _mm_set1_epi8(0x7E);
    __m128i non_ascii_mask = _mm_set1_epi8(0b10000000);
    __m128i pound = _mm_set1_epi8('#');
    __m128i plus = _mm_set1_epi8('+');

public:
    SimdUtils() = default;

    std::vector<std::string> splitTopic(const std::string &topic);
    bool isValidUtf8(const std::string &s, bool alsoCheckInvalidPublishChars = false);
};

#endif


#endif // THREADLOCALUTILS_H
