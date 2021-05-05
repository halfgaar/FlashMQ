#ifndef THREADLOCALUTILS_H
#define THREADLOCALUTILS_H

#include <vector>
#include <string>
#include <immintrin.h>

#define TOPIC_MEMORY_LENGTH 65560

/**
 * @brief The Utils class have utility functions that make use of pre-allocated memory. Use with thread_local or create per thread manually.
 */
class Utils
{
    std::vector<std::string> subtopics;
    std::vector<char> subtopicParseMem;
    std::vector<char> topicCopy;
    __m128i slashes = _mm_set1_epi8('/');
    __m128i lowerBound = _mm_set1_epi8(0x20);
    __m128i lastAsciiChar = _mm_set1_epi8(0x7E);
    __m128i non_ascii_mask = _mm_set1_epi8(0b10000000);
    __m128i pound = _mm_set1_epi8('#');
    __m128i plus = _mm_set1_epi8('+');

public:
    Utils();

    std::vector<std::string> *splitTopic(const std::string &topic);
    bool isValidUtf8(const std::string &s, bool alsoCheckInvalidPublishChars = false);
};


#endif // THREADLOCALUTILS_H
