#ifndef UTILS_H
#define UTILS_H

#include <string.h>
#include <errno.h>
#include <string>
#include <list>
#include <limits>
#include <vector>

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

bool topicsMatch(const std::string &subscribeTopic, const std::string &publishTopic);

bool isValidUtf8(const std::string &s);

bool strContains(const std::string &s, const std::string &needle);

bool isValidPublishPath(const std::string &s);

#endif // UTILS_H
