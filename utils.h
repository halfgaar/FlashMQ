/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef UTILS_H
#define UTILS_H

#include <string.h>
#include <errno.h>
#include <string>
#include <list>
#include <limits>
#include <vector>
#include <algorithm>
#include <openssl/evp.h>
#include <memory>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <stdexcept>
#include <cassert>

#include "cirbuf.h"
#include "bindaddr.h"
#include "types.h"
#include "flashmq_plugin.h"
#include "threadlocalutils.h"

#define UNUSED(expr) do { (void)(expr); } while (0)

template<typename T> int check(int rc)
{
    if (rc < 0)
    {
        char *err = strerror(errno);
        std::string msg(err);
        throw T(msg);
    }

    return rc;
}

std::list<std::string> split(const std::string &input, const char sep, size_t max = std::numeric_limits<int>::max(), bool keep_empty_parts = true);
std::vector<std::string> splitToVector(const std::string &input, const char sep, size_t max = std::numeric_limits<int>::max(), bool keep_empty_parts = true);
std::vector<std::string> splitTopic(const std::string &topic);

bool isValidUtf8Generic(const char *s, bool alsoCheckInvalidPublishChars = false);

template<typename T>
bool isValidUtf8Generic(const T &s, bool alsoCheckInvalidPublishChars = false)
{
    int multibyte_remain = 0;
    uint32_t cur_code_point = 0;
    int total_char_len = 0;
    for(const uint8_t x : s)
    {
        if (alsoCheckInvalidPublishChars && (x == '#' || x == '+'))
            return false;

        if(!multibyte_remain)
        {
            cur_code_point = 0;

            if ((x & 0b10000000) == 0) // when the MSB is 0, it's ASCII, most common case
            {
                cur_code_point += (x & 0b01111111);
            }
            else if((x & 0b11100000) == 0b11000000) // 2 byte char
            {
                multibyte_remain = 1;
                cur_code_point += ((x & 0b00011111) << 6);
            }
            else if((x & 0b11110000) == 0b11100000) // 3 byte char
            {
                multibyte_remain = 2;
                cur_code_point += ((x & 0b00001111) << 12);
            }
            else if((x & 0b11111000) == 0b11110000) // 4 byte char
            {
                multibyte_remain = 3;
                cur_code_point += ((x & 0b00000111) << 18);
            }
            else if((x & 0b10000000) != 0)
                return false;
            else
                cur_code_point += (x & 0b01111111);

            total_char_len = multibyte_remain + 1;
        }
        else // All remainer bytes of this code point needs to start with 10
        {
            if((x & 0b11000000) != 0b10000000)
                return false;
            multibyte_remain--;
            cur_code_point += ((x & 0b00111111) << (6*multibyte_remain));
        }

        if (multibyte_remain == 0)
        {
            // Check overlong values, to avoid having mulitiple representations of the same value.
            if (total_char_len == 1)
            {

            }
            else if (total_char_len == 2 && cur_code_point < 0x80)
                return false;
            else if (total_char_len == 3 && cur_code_point < 0x800)
                return false;
            else if (total_char_len == 4 && cur_code_point < 0x10000)
                return false;

            if (cur_code_point <= 0x001F)
                return false;
            if (cur_code_point >= 0x007F && cur_code_point <= 0x009F)
                return false;

            if (total_char_len > 1)
            {
                // Invalid range for MQTT. [MQTT-1.5.3-1]
                if (cur_code_point >= 0xD800 && cur_code_point <= 0xDFFF) // Dec 55296-57343
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

            cur_code_point = 0;
        }
    }
    return multibyte_remain == 0;
}

template<typename T>
bool isValidUtf8(const T &s, bool alsoCheckInvalidPublishChars = false)
{
#ifdef __SSE4_2__
    thread_local static SimdUtils simdUtils;
    return simdUtils.isValidUtf8(s, alsoCheckInvalidPublishChars);
#else
    return isValidUtf8Generic(s, alsoCheckInvalidPublishChars);
#endif
}

bool strContains(const std::string &s, const std::string &needle);

bool isValidShareName(const std::string &s);
bool isValidPublishPath(const std::string &s);
bool isValidSubscribePath(const std::string &s);
bool containsDangerousCharacters(const std::string &s);

void ltrim(std::string &s);
void rtrim(std::string &s);
void trim(std::string &s);
bool startsWith(const std::string &s, const std::string &needle);
bool endsWith(const std::string &s, const std::string &ending);
std::string &rtrim(std::string &s, unsigned char c);

std::string getSecureRandomString(const ssize_t len);
std::string str_tolower(std::string s);
bool stringTruthiness(const std::string &val);
bool isPowerOfTwo(int val);

std::vector<char> base64Decode(const std::string &s);
std::string base64Encode(const unsigned char *input, const int length);


void testSsl(const std::string &fullchain, const std::string &privkey);
void testSslVerifyLocations(const std::string &caFile, const std::string &caDir, const std::string &error);

std::string formatString(const std::string str, ...);

std::string_view dirnameOf(std::string_view fname);

BindAddr getBindAddr(int family, const std::string &bindAddress, int port);

size_t getFileSize(const std::string &path);
size_t getFreeSpace(const std::string &path);

sa_family_t getFamilyFromSockAddr(const struct sockaddr *addr);
std::string sockaddrToString(const struct sockaddr *addr);

template<typename ex> void checkWritableDir(const std::string &path)
{
    if (path.empty())
        throw ex("Dir path to check is an empty string.");

    if (access(path.c_str(), W_OK) != 0)
    {
        std::string msg = formatString("Path '%s' is not there or not writable", path.c_str());
        throw ex(msg);
    }

    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    if (stat(path.c_str(), &statbuf) < 0)
    {
        // We checked for W_OK above, so this shouldn't happen.
        std::string msg = formatString("Error getting information about '%s'.", path.c_str());
        throw ex(msg);
    }

    if (!S_ISDIR(statbuf.st_mode))
    {
        std::string msg = formatString("Path '%s' is not a directory.", path.c_str());
        throw ex(msg);
    }
}

std::string protocolVersionString(ProtocolVersion p);

unsigned int distanceBetweenStrings(const std::string &stringA, const std::string &stringB);

template<typename it>
it findCloseStringMatch(it first, it last, const std::string &s)
{
    it alternative = last;
    unsigned int alternative_distance = UINT_MAX;

    for (auto possible_key = first; possible_key != last; ++possible_key)
    {
        unsigned int distance = distanceBetweenStrings(s, *possible_key);

        // We only want to suggest options that look a bit like the unknown
        // one. Experimentally I found 50% of the total length a decent
        // cutoff.
        //
        // The mathemathical formula "distance/length < 0.5" can be
        // approximated with integers as "distance*2/length < 1"
        if ((distance * 2) / s.length() < 1 && distance < alternative_distance)
        {
            alternative = possible_key;
            alternative_distance = distance;
        }
    }

    return alternative;
}

uint32_t ageFromTimePoint(const std::chrono::time_point<std::chrono::steady_clock> &point);
std::chrono::time_point<std::chrono::steady_clock> timepointFromAge(const uint32_t age);

ReasonCodes authResultToReasonCode(AuthResult authResult);

int maskAllSignalsCurrentThread();

void parseSubscriptionShare(std::vector<std::string> &subtopics, std::string &shareName, std::string &topic);

std::string timestampWithMillis();

template<class T>
T get_random_int()
{
    std::vector<T> buf(1);
    // We use urandom, so we don't have check for blocking / interrupted conditions.
    if (getrandom(buf.data(), sizeof(T), 0) < 0)
        throw std::runtime_error(strerror(errno));
    T val = buf.at(0);
    return val;
}

void exceptionOnNonMqtt(const std::vector<char> &data);

uint16_t getFirstWildcardDepth(const std::vector<std::string> &subtopics);
std::string reasonCodeToString(ReasonCodes code);
std::string packetTypeToString(PacketType ptype);
std::string propertyToString(Mqtt5Properties p);


/**
 * @brief parseValuesWithOptionalQuoting parses argument lists space encoded, with quote and escaping support.
 * @param s
 * @return
 *
 * So, like
 *
 *  "hallo" "you": becomes a vector with those two strings, but without the quote.
 *  hallo  you: is the same as above.
 *  "hallo" you: is the same as above.
 *  "hallo you": is a vector with one element.
 *  "I quote you with \"" is: I quote you with "
 *  'I quote you with "'  is: I quote you with "
 */
template<typename ex>
std::vector<std::string> parseValuesWithOptionalQuoting(std::string s)
{
    trim(s);
    std::vector<std::string> result;

    char quote = 0;
    std::string cur;
    bool escape = false;

    for (char c : s)
    {
        if (escape)
        {
            if (!(c == '"' || c == '\'' || c == '\\'))
                throw ex("Invalid escape");

            cur.push_back(c);
            escape = false;
        }
        else if (c == '\\')
        {
            escape = true;
        }
        else if (std::isspace(c))
        {
            if (quote)
                cur.push_back(c);
            else if (!cur.empty())
            {
                result.push_back(cur);
                cur.clear();
            }
        }
        else if (c == '"' || c == '\'')
        {
            if (quote == 0)
            {
                quote = c;
            }
            else if (quote == c)
            {
                result.push_back(cur);
                cur.clear();
                quote = 0;
            }
            else
            {
                cur.push_back(c);
            }
        }
        else
        {
            cur.push_back(c);
        }
    }

    if (!cur.empty())
        result.push_back(cur);

    if (quote)
        throw ex("Unterminated quote");

    return result;
}

template<typename T>
class DecrementGuard
{
    T &n;
public:
    DecrementGuard(T &n) :
        n(n)
    {

    }

    ~DecrementGuard()
    {
        n--;
        assert(n >= 0);
    }
};

template<typename T>
std::optional<T> &non_optional(std::optional<T> &o)
{
    if (!o)
        o.emplace();
    return o;
}


#endif // UTILS_H
