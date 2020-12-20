#include "utils.h"



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
