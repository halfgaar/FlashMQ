#include "utils.h"

#include "sys/time.h"
#include "sys/random.h"
#include <algorithm>

#include "exceptions.h"

std::list<std::__cxx11::string> split(const std::string &input, const char sep, size_t max, bool keep_empty_parts)
{
    std::list<std::string> list;
    size_t start = 0;
    size_t end;

    while (list.size() < max && (end = input.find(sep, start)) != std::string::npos)
    {
        if (start != end || keep_empty_parts)
            list.push_back(input.substr(start, end - start));
        start = end + 1; // increase by length of seperator.
    }
    if (start != input.size() || keep_empty_parts)
        list.push_back(input.substr(start, std::string::npos));
    return list;
}


bool topicsMatch(const std::string &subscribeTopic, const std::string &publishTopic)
{
    if (subscribeTopic.find("+") == std::string::npos && subscribeTopic.find("#") == std::string::npos)
        return subscribeTopic == publishTopic;

    const std::list<std::string> subscribeParts = split(subscribeTopic, '/');
    const std::list<std::string> publishParts = split(publishTopic, '/');

    auto subscribe_itr = subscribeParts.begin();
    auto publish_itr = publishParts.begin();

    bool result = true;
    while (subscribe_itr != subscribeParts.end() && publish_itr != publishParts.end())
    {
        const std::string &subscribe_subtopic = *subscribe_itr++;
        const std::string &publish_subtopic = *publish_itr++;

        if (subscribe_subtopic == "+")
            continue;
        if (subscribe_subtopic == "#")
            return true;
        if (subscribe_subtopic != publish_subtopic)
            return false;
    }

    result = subscribe_itr == subscribeParts.end() && publish_itr == publishParts.end();
    return result;
}

bool isValidUtf8(const std::string &s)
{
    int multibyte_remain = 0;
    int cur_code_point = 0;
    for(const char &x : s)
    {
        if (x == 0)
            return false;

        if(!multibyte_remain)
        {
            cur_code_point = 0;

            if((x & 0b11100000) == 0b11000000) // 2 byte char
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
            // Invalid range for MQTT. [MQTT-1.5.3-1]
            if (cur_code_point >= 0xD800 && cur_code_point <= 0xDFFF) // Dec 55296-57343
                return false;
            if (cur_code_point >= 0x0001 && cur_code_point <= 0x001F)
                return false;
            if (cur_code_point >= 0x007F && cur_code_point <= 0x009F)
                return false;
            if (cur_code_point == 0xFFFF)
                return false;
            cur_code_point = 0;
        }
    }
    return multibyte_remain == 0;
}

bool strContains(const std::string &s, const std::string &needle)
{
    return s.find(needle) != std::string::npos;
}

bool isValidPublishPath(const std::string &s)
{
    if (s.empty())
        return false;

    for (const char c : s)
    {
        if (c == '#' || c == '+')
            return false;
    }

    return true;
}

std::vector<std::string> splitToVector(const std::string &input, const char sep, size_t max, bool keep_empty_parts)
{
    std::vector<std::string> list;
    list.reserve(16); // This is somewhat arbitratry, knowing some typical MQTT topic use cases
    size_t start = 0;
    size_t end;

    while (list.size() < max && (end = input.find(sep, start)) != std::string::npos)
    {
        if (start != end || keep_empty_parts)
            list.push_back(input.substr(start, end - start));
        start = end + 1; // increase by length of seperator.
    }
    if (start != input.size() || keep_empty_parts)
        list.push_back(input.substr(start, std::string::npos));
    return list;
}

void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

bool startsWith(const std::string &s, const std::string &needle)
{
    return s.find(needle) == 0;
}

int64_t currentMSecsSinceEpoch()
{
    struct timeval te;
    gettimeofday(&te, NULL);
    int64_t milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
    return milliseconds;
}

std::string getSecureRandomString(const size_t len)
{
    std::vector<char> buf(len);
    size_t actual_len = getrandom(buf.data(), len, 0);

    if (actual_len < 0 || actual_len != len)
    {
        throw std::runtime_error("Error requesting random data");
    }

    const std::string possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrtsuvwxyz1234567890");
    const int possibleCharactersCount = possibleCharacters.length();

    std::string randomString;
    for(const unsigned char &c : buf)
    {
        unsigned int index = c % possibleCharactersCount;
        char nextChar = possibleCharacters.at(index);
        randomString.push_back(nextChar);
    }
    return randomString;
}

std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

bool stringTruthiness(const std::string &val)
{
    std::string val_ = str_tolower(val);
    trim(val_);
    if (val_ == "yes" || val_ == "true" || val_ == "on")
        return  true;
    if (val_ == "no" || val_ == "false" || val_ == "off")
        return false;
    throw ConfigFileException("Value '" + val + "' can't be converted to boolean");
}
