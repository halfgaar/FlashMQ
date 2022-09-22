#ifdef __SSE4_2__

#include "threadlocalutils.h"

#include <cstring>
#include <cassert>

SimdUtils::SimdUtils() :
    subtopicParseMem(TOPIC_MEMORY_LENGTH),
    topicCopy(TOPIC_MEMORY_LENGTH)
{

}

/**
 * @brief SimdUtils::splitTopic uses SSE4.2 to detect the '/' chars, 16 chars at a time, and returns a pointer to thread-local memory.
 * @param topic
 * @param output is cleared and emplaced in. You can give it members from the Utils class, to avoid re-allocation.
 * @return
 */
std::vector<std::string> *SimdUtils::splitTopic(const std::string &topic, std::vector<std::string> &output)
{
    output.clear();

    const int s = topic.size();
    std::memcpy(topicCopy.data(), topic.c_str(), s+1);
    std::memset(&topicCopy.data()[s], 0, 16);
    int n = 0;
    int carryi = 0;
    while (n <= s)
    {
        const char *i = &topicCopy.data()[n];
        __m128i loaded = _mm_loadu_si128((__m128i*)i);

        int len_left = s - n;
        assert(len_left >= 0);
        int index = _mm_cmpestri(slashes, 1, loaded, len_left, 0);
        std::memcpy(&subtopicParseMem[carryi], i, index);
        carryi += std::min<int>(index, len_left);

        n += index;

        if (index < 16 || n >= s)
        {
            output.emplace_back(subtopicParseMem.data(), carryi);
            carryi = 0;
            n++;
        }
    }

    return &output;
}

/**
 * @brief SimdUtils::isValidUtf8 checks UTF-8 validity 16 bytes at a time, using SSE 4.2.
 * @param s
 * @param alsoCheckInvalidPublishChars is for checking the presence of '#' and '+' which is not allowed in publishes.
 * @return
 */
bool SimdUtils::isValidUtf8(const std::string &s, bool alsoCheckInvalidPublishChars)
{
    const int len = s.size();

    if (len + 16 > TOPIC_MEMORY_LENGTH)
        return false;

    std::memcpy(topicCopy.data(), s.c_str(), len);
    std::memset(&topicCopy.data()[len], 0x20, 16); // I fill out with spaces, as valid chars

    int n = 0;
    const char *i = topicCopy.data();
    while (n < len)
    {
        const int len_left = len - n;
        assert(len_left > 0);
        __m128i loaded = _mm_loadu_si128((__m128i*)&i[n]);
        __m128i loaded_AND_non_ascii = _mm_and_si128(loaded, non_ascii_mask);

        if (alsoCheckInvalidPublishChars && (_mm_movemask_epi8(_mm_cmpeq_epi8(loaded, pound) || _mm_movemask_epi8(_mm_cmpeq_epi8(loaded, plus)))))
            return false;

        int index = _mm_cmpestri(non_ascii_mask, 1, loaded_AND_non_ascii, len_left, 0);
        n += index;

        // Checking multi-byte chars one by one. With some effort, this may be done using SIMD too, but the majority of uses will
        // have a minimum of multi byte chars.
        if (index < 16)
        {
            char x = i[n++];
            char char_len = 0;
            int cur_code_point = 0;

            if((x & 0b11100000) == 0b11000000) // 2 byte char
            {
                char_len = 1;
                cur_code_point += ((x & 0b00011111) << 6);
            }
            else if((x & 0b11110000) == 0b11100000) // 3 byte char
            {
                char_len = 2;
                cur_code_point += ((x & 0b00001111) << 12);
            }
            else if((x & 0b11111000) == 0b11110000) // 4 byte char
            {
                char_len = 3;
                cur_code_point += ((x & 0b00000111) << 18);
            }
            else
                return false;

            while (char_len > 0)
            {
                if (n >= len)
                    return false;

                x = i[n++];

                if((x & 0b11000000) != 0b10000000) // All remainer bytes of this code point needs to start with 10
                    return false;
                char_len--;
                cur_code_point += ((x & 0b00111111) << (6*char_len));
            }

            if (cur_code_point >= 0xD800 && cur_code_point <= 0xDFFF) // Dec 55296-57343
                return false;

            if (cur_code_point == 0xFFFF)
                return false;
        }
        else
        {
            if (_mm_movemask_epi8(_mm_cmplt_epi8(loaded, lowerBound)))
                return false;

            if (_mm_movemask_epi8(_mm_cmpgt_epi8(loaded, lastAsciiChar)))
                return false;
        }

    }

    return true;
}

#endif
