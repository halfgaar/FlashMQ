#ifdef __SSE4_2__

#include "threadlocalutils.h"

#include <cstring>
#include <cassert>

SimdUtils::SimdUtils() :
    topicCopy(TOPIC_MEMORY_LENGTH)
{

}

/**
 * @brief SimdUtils::splitTopic uses SSE4.2 to detect the '/' chars, 16 chars at a time.
 * @param topic
 * @param output is cleared and emplaced in.
 * @return
 *
 * The SSE instructions are placed so that the CPU pipeline can continue with other things without having to wait for the
 * result (because _mm_loadu_si128 and _mm_cmpestri have a latency of 6 and 18, respectively).
 *
 * Unfortunately, the string construction is the slowest. Without the emplace, 15 million topics take
 * about 1000 ms. Without, 180 ms. (On a 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz).
 */
std::vector<std::string> *SimdUtils::splitTopic(const std::string &topic, std::vector<std::string> &output)
{
    const char *src_mem = topic.data();
    __m128i loaded = _mm_loadu_si128((__m128i*)src_mem);

    output.clear();
    output.reserve(16);

    const int slen = topic.size();

    int start = 0;
    int pos = 0;
    int sub_len = 0;
    while (pos <= slen) // The 'equals' part looks weird, but it's necessary for strings ending in the separator.
    {
        int len_left = slen - pos;
        assert(len_left >= 0);
        int index = _mm_cmpestri(slashes, 1, loaded, len_left, 0);
        sub_len += std::min<int>(index, len_left);

        pos += index;

        if (index < 16 || pos >= slen)
        {
            pos++; // skip over the seperator
            if (pos < slen)
            {
                loaded = _mm_loadu_si128((__m128i*)(src_mem + pos));
            }
            assert(start <= slen);
            assert(start + sub_len <= slen);
            output.emplace_back(src_mem + start, sub_len); // Takes the bulk of the time, about 80%.
            sub_len = 0;
            start = pos;
        }
        else
        {
            assert(pos < slen);
            loaded = _mm_loadu_si128((__m128i*)(src_mem + pos));
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
            uint8_t x = i[n++];
            int8_t char_len_left = 0;
            int8_t total_char_len = 0;
            uint32_t cur_code_point = 0;

            if((x & 0b11100000) == 0b11000000) // 2 byte char
            {
                char_len_left = 1;
                cur_code_point += ((x & 0b00011111) << 6);
            }
            else if((x & 0b11110000) == 0b11100000) // 3 byte char
            {
                char_len_left = 2;
                cur_code_point += ((x & 0b00001111) << 12);
            }
            else if((x & 0b11111000) == 0b11110000) // 4 byte char
            {
                char_len_left = 3;
                cur_code_point += ((x & 0b00000111) << 18);
            }
            else
                return false;

            total_char_len = char_len_left + 1;

            while (char_len_left > 0)
            {
                if (n >= len)
                    return false;

                x = i[n++];

                if((x & 0b11000000) != 0b10000000) // All remainer bytes of this code point needs to start with 10
                    return false;
                char_len_left--;
                cur_code_point += ((x & 0b00111111) << (6*char_len_left));
            }

            // Check overlong values, to avoid having mulitiple representations of the same value.
            if (total_char_len == 2 && cur_code_point < 0x80)
                return false;
            else if (total_char_len == 3 && cur_code_point < 0x800)
                return false;
            else if (total_char_len == 4 && cur_code_point < 0x10000)
                return false;

            if (cur_code_point >= 0xD800 && cur_code_point <= 0xDFFF) // Dec 55296-57343
                return false;

            if (cur_code_point >= 0x7F && cur_code_point <= 0x009F)
                return false;

            // Unicode spec: "Which code points are noncharacters?".
            if (cur_code_point >= 0xFDD0 && cur_code_point <= 0xFDEF)
                return false;
            // The last two code points of each of the 17 planes are the remaining 34 non-chars.
            const uint32_t plane = (cur_code_point & 0x1F0000) >> 16;
            const uint32_t last_16_bit = cur_code_point & 0xFFFF;
            if (plane <= 16 && (last_16_bit == 0xFFFE || last_16_bit == 0xFFFF))
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
