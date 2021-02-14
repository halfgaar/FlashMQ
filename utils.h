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

#include "cirbuf.h"

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

void ltrim(std::string &s);
void rtrim(std::string &s);
void trim(std::string &s);
bool startsWith(const std::string &s, const std::string &needle);

int64_t currentMSecsSinceEpoch();
std::string getSecureRandomString(const size_t len);
std::string str_tolower(std::string s);
bool stringTruthiness(const std::string &val);
bool isPowerOfTwo(int val);

bool parseHttpHeader(CirBuf &buf, std::string &websocket_key, int &websocket_version);

std::string base64Encode(const unsigned char *input, const int length);
std::string generateWebsocketAcceptString(const std::string &websocketKey);

std::string generateInvalidWebsocketVersionHttpHeaders(const int wantedVersion);
std::string generateBadHttpRequestReponse(const std::string &msg);
std::string generateWebsocketAnswer(const std::string &acceptString);

#endif // UTILS_H
