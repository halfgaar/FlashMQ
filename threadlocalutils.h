#ifndef THREADLOCALUTILS_H
#define THREADLOCALUTILS_H

#ifdef __SSE4_2__

#include <vector>
#include <string>
#include <immintrin.h>

#define TOPIC_MEMORY_LENGTH 65560



class SimdUtils
{
    std::vector<char> subtopicParseMem;
    std::vector<char> topicCopy;
    __m128i slashes = _mm_set1_epi8('/');
    __m128i lowerBound = _mm_set1_epi8(0x20);
    __m128i lastAsciiChar = _mm_set1_epi8(0x7E);
    __m128i non_ascii_mask = _mm_set1_epi8(0b10000000);
    __m128i pound = _mm_set1_epi8('#');
    __m128i plus = _mm_set1_epi8('+');

public:
    SimdUtils();

    std::vector<std::string> *splitTopic(const std::string &topic, std::vector<std::string> &output);
    bool isValidUtf8(const std::string &s, bool alsoCheckInvalidPublishChars = false);
};

#endif


#endif // THREADLOCALUTILS_H
