/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#include "sys/stat.h"

#include "utils.h"

#include "sys/time.h"
#include "sys/random.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "openssl/ssl.h"
#include "openssl/err.h"

#include "exceptions.h"
#include "cirbuf.h"
#include "sslctxmanager.h"
#include "logger.h"
#include "evpencodectxmanager.h"


#ifdef __SSE4_2__
#include "threadlocalutils.h"
thread_local SimdUtils simdUtils;
#endif

std::list<std::string> split(const std::string &input, const char sep, size_t max, bool keep_empty_parts)
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

thread_local std::vector<std::string> subscribeParts;
thread_local std::vector<std::string> publishParts;
bool topicsMatch(const std::string &subscribeTopic, const std::string &publishTopic)
{
    if (subscribeTopic.find("+") == std::string::npos && subscribeTopic.find("#") == std::string::npos)
        return subscribeTopic == publishTopic;

    if (!subscribeTopic.empty() && !publishTopic.empty() && publishTopic[0] == '$' && subscribeTopic[0] != '$')
        return false;

    subscribeParts.clear();
    publishParts.clear();

#ifdef __SSE4_2__
    simdUtils.splitTopic(subscribeTopic, subscribeParts);
    simdUtils.splitTopic(publishTopic, publishParts);
#else
    splitToVector(subscribeTopic, subscribeParts, '/');
    splitToVector(publishTopic, publishParts, '/');
#endif

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

bool isValidUtf8Generic(const std::string &s, bool alsoCheckInvalidPublishChars)
{
    int multibyte_remain = 0;
    int cur_code_point = 0;
    for(const char x : s)
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
            if (cur_code_point <= 0x001F)
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

bool isValidUtf8(const std::string &s, bool alsoCheckInvalidPublishChars)
{
#ifdef __SSE4_2__
    return simdUtils.isValidUtf8(s, alsoCheckInvalidPublishChars);
#else
    return isValidUtf8Generic(s, alsoCheckInvalidPublishChars);
#endif
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

bool isValidSubscribePath(const std::string &s)
{
    bool plusAllowed = true;
    bool nextMustBeSlash = false;
    bool poundSeen = false;

    for (const char c : s)
    {
        if (!plusAllowed && c == '+')
            return false;

        if (nextMustBeSlash && c != '/')
            return false;

        if (poundSeen)
            return false;

        plusAllowed = c == '/';
        nextMustBeSlash = c == '+';
        poundSeen = c == '#';
    }

    return true;
}

bool containsDangerousCharacters(const std::string &s)
{
    if (s.empty())
        return false;

    for (const char c : s)
    {
        switch(c)
        {
        case '#':
            return  true;
        case '+':
            return  true;
        }
    }

    return false;
}

void splitTopic(const std::string &topic, std::vector<std::string> &output)
{
#ifdef __SSE4_2__
    simdUtils.splitTopic(topic, output);
#else
    splitToVector(topic, output, '/');
#endif
}

void splitToVector(const std::string &input, std::vector<std::string> &output, const char sep, size_t max, bool keep_empty_parts)
{
    const auto subtopic_count = std::count(input.begin(), input.end(), '/') + 1;

    output.reserve(subtopic_count);
    size_t start = 0;
    size_t end;

    const auto npos = std::string::npos;

    while (output.size() < max && (end = input.find(sep, start)) != npos)
    {
        if (start != end || keep_empty_parts)
            output.push_back(input.substr(start, end - start));
        start = end + 1; // increase by length of seperator.
    }
    if (start != input.size() || keep_empty_parts)
        output.push_back(input.substr(start, npos));
}

const std::vector<std::string> splitToVector(const std::string &input, const char sep, size_t max, bool keep_empty_parts)
{
    std::vector<std::string> result;
    splitToVector(input, result, sep, max, keep_empty_parts);
    return result;
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

std::string &rtrim(std::string &s, unsigned char c)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [=](unsigned char ch) {
        return (c != ch);
    }).base(), s.end());
    return s;
}

bool startsWith(const std::string &s, const std::string &needle)
{
    return s.find(needle) == 0;
}

std::string getSecureRandomString(const ssize_t len)
{
    std::vector<unsigned char> buf(len);
    ssize_t actual_len = getrandom(buf.data(), len, 0);

    if (actual_len < 0 || actual_len != len)
    {
        throw std::runtime_error("Error requesting random data");
    }

    const std::string possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrtsuvwxyz1234567890");
    const int possibleCharactersCount = possibleCharacters.length();

    std::string randomString;
    for(const unsigned char c : buf)
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

bool isPowerOfTwo(int n)
{
    return (n != 0) && (n & (n - 1)) == 0;
}

bool parseHttpHeader(CirBuf &buf, std::string &websocket_key, int &websocket_version)
{
    const std::string s(buf.tailPtr(), buf.usedBytes());
    std::istringstream is(s);
    bool doubleEmptyLine = false; // meaning, the HTTP header is complete
    bool upgradeHeaderSeen = false;
    bool connectionHeaderSeen = false;
    bool firstLine = true;
    bool subprotocol_seen = false;

    std::string line;
    while (std::getline(is, line))
    {
        trim(line);
        if (firstLine)
        {
            firstLine = false;
            if (!startsWith(line, "GET"))
                throw BadHttpRequest("Websocket request should start with GET.");
            continue;
        }
        if (line.empty())
        {
            doubleEmptyLine = true;
            break;
        }

        std::list<std::string> fields = split(line, ':', 1);

        if (fields.size() != 2)
        {
            throw BadHttpRequest("This does not look like a HTTP request.");
        }

        const std::vector<std::string> fields2(fields.begin(), fields.end());
        std::string name = str_tolower(fields2[0]);
        trim(name);
        std::string value = fields2[1];
        trim(value);
        std::string value_lower = str_tolower(value);

        if (name == "upgrade" && value_lower == "websocket")
            upgradeHeaderSeen = true;
        else if (name == "connection" && strContains(value_lower, "upgrade"))
            connectionHeaderSeen = true;
        else if (name == "sec-websocket-key")
            websocket_key = value;
        else if (name == "sec-websocket-version")
            websocket_version = stoi(value);
        else if (name == "sec-websocket-protocol" && value_lower == "mqtt")
            subprotocol_seen = true;
    }

    if (doubleEmptyLine)
    {
        if (!connectionHeaderSeen || !upgradeHeaderSeen)
            throw BadHttpRequest("HTTP request is not a websocket upgrade request.");
        if (!subprotocol_seen)
            throw BadHttpRequest("HTTP header Sec-WebSocket-Protocol with value 'mqtt' must be present.");
    }

    return doubleEmptyLine;
}

std::vector<char> base64Decode(const std::string &s)
{
    if (s.length() % 4 != 0)
        throw std::runtime_error("Decoding invalid base64 string");

    if (s.empty())
        throw std::runtime_error("Trying to base64 decode an empty string.");

    std::vector<char> tmp(s.size());

    int outl = 0;
    int outl_total = 0;

    EvpEncodeCtxManager b64_ctx;
    if (EVP_DecodeUpdate(b64_ctx.ctx, reinterpret_cast<unsigned char*>(tmp.data()), &outl, reinterpret_cast<const unsigned char*>(s.c_str()), s.size()) < 0)
        throw std::runtime_error("Failure in EVP_DecodeUpdate()");
    outl_total += outl;
    if (EVP_DecodeFinal(b64_ctx.ctx, reinterpret_cast<unsigned char*>(tmp[outl_total]), &outl) < 0)
        throw std::runtime_error("Failure in EVP_DecodeFinal()");
    std::vector<char> result(outl_total);
    std::memcpy(result.data(), tmp.data(), outl_total);
    return result;
}

std::string base64Encode(const unsigned char *input, const int length)
{
    const int pl = 4*((length+2)/3);
    char *output = reinterpret_cast<char *>(calloc(pl+1, 1));
    const int ol = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(output), input, length);
    std::string result(output);
    free(output);

    if (pl != ol)
        throw std::runtime_error("Base64 encode error.");

    return result;
}

std::string generateWebsocketAcceptString(const std::string &websocketKey)
{
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();;
    const EVP_MD *md = EVP_sha1();
    EVP_DigestInit_ex(mdctx, md, NULL);

    const std::string keyPlusMagic = websocketKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    EVP_DigestUpdate(mdctx, keyPlusMagic.c_str(), keyPlusMagic.length());
    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_free(mdctx);

    std::string base64 = base64Encode(md_value, md_len);
    return base64;
}

std::string generateInvalidWebsocketVersionHttpHeaders(const int wantedVersion)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 400 Bad Request\r\n";
    oss << "Sec-WebSocket-Version: " << wantedVersion;
    oss << "\r\n";
    oss.flush();
    return oss.str();
}

std::string generateBadHttpRequestReponse(const std::string &msg)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 400 Bad Request\r\n";
    oss << "\r\n";
    oss << msg;
    oss.flush();
    return oss.str();
}

std::string generateWebsocketAnswer(const std::string &acceptString)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 101 Switching Protocols\r\n";
    oss << "Upgrade: websocket\r\n";
    oss << "Connection: Upgrade\r\n";
    oss << "Sec-WebSocket-Accept: " << acceptString << "\r\n";
    oss << "Sec-WebSocket-Protocol: mqtt\r\n";
    oss << "\r\n";
    oss.flush();
    return oss.str();
}

// Using a separate ssl context to test, because it's the easiest way to load certs and key atomitcally.
void testSsl(const std::string &fullchain, const std::string &privkey)
{
    if (fullchain.empty() && privkey.empty())
        throw ConfigFileException("No privkey and fullchain specified.");

    if (fullchain.empty())
        throw ConfigFileException("No private key specified for fullchain");

    if (privkey.empty())
        throw ConfigFileException("No fullchain specified for private key");

    if (getFileSize(fullchain) == 0)
        throw ConfigFileException(formatString("SSL 'fullchain' file '%s' is empty or invalid", fullchain.c_str()));

    if (getFileSize(privkey) == 0)
        throw ConfigFileException(formatString("SSL 'privkey' file '%s' is empty or invalid", privkey.c_str()));

    SslCtxManager sslCtx;
    if (SSL_CTX_use_certificate_file(sslCtx.get(), fullchain.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        ERR_print_errors_cb(logSslError, NULL);
        throw ConfigFileException("Error loading full chain " + fullchain);
    }
    if (SSL_CTX_use_PrivateKey_file(sslCtx.get(), privkey.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        ERR_print_errors_cb(logSslError, NULL);
        throw ConfigFileException("Error loading private key " + privkey);
    }
    if (SSL_CTX_check_private_key(sslCtx.get()) != 1)
    {
        ERR_print_errors_cb(logSslError, NULL);
        throw ConfigFileException("Private key and certificate don't match.");
    }
}

std::string formatString(const std::string str, ...)
{
    char buf[512];

    va_list valist;
    va_start(valist, str);
    vsnprintf(buf, 512, str.c_str(), valist);
    va_end(valist);

    size_t len = strlen(buf);
    std::string result(buf, len);

    return result;
}

std::string dirnameOf(const std::string& path)
{
     size_t pos = path.find_last_of("\\/");
     return (std::string::npos == pos) ? "" : path.substr(0, pos);
}


BindAddr getBindAddr(int family, const std::string &bindAddress,  int port)
{
    BindAddr result;

    if (family == AF_INET)
    {
        struct sockaddr_in *in_addr_v4 = new sockaddr_in();
        result.len = sizeof(struct sockaddr_in);
        memset(in_addr_v4, 0, result.len);

        if (bindAddress.empty())
            in_addr_v4->sin_addr.s_addr = INADDR_ANY;
        else
            inet_pton(AF_INET, bindAddress.c_str(), &in_addr_v4->sin_addr);

        in_addr_v4->sin_family = AF_INET;
        in_addr_v4->sin_port = htons(port);
        result.p.reset(reinterpret_cast<sockaddr*>(in_addr_v4));
    }
    if (family == AF_INET6)
    {
        struct sockaddr_in6 *in_addr_v6 = new sockaddr_in6();
        result.len = sizeof(struct sockaddr_in6);
        memset(in_addr_v6, 0, result.len);

        if (bindAddress.empty())
            in_addr_v6->sin6_addr = IN6ADDR_ANY_INIT;
        else
            inet_pton(AF_INET6, bindAddress.c_str(), &in_addr_v6->sin6_addr);

        in_addr_v6->sin6_family = AF_INET6;
        in_addr_v6->sin6_port = htons(port);
        result.p.reset(reinterpret_cast<sockaddr*>(in_addr_v6));
    }

    return result;
}

ssize_t getFileSize(const std::string &path)
{
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    if (stat(path.c_str(), &statbuf) < 0)
        return -1;

    return statbuf.st_size;
}

std::string sockaddrToString(sockaddr *addr)
{
    if (!addr)
        return "[unknown address]";

    char buf[INET6_ADDRSTRLEN];
    void *addr_in = nullptr;

    if (addr->sa_family == AF_INET)
    {
        struct sockaddr_in *ipv4sockAddr = reinterpret_cast<struct sockaddr_in*>(addr);
        addr_in = &ipv4sockAddr->sin_addr;
    }
    else if (addr->sa_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6sockAddr = reinterpret_cast<struct sockaddr_in6*>(addr);
        addr_in = &ipv6sockAddr->sin6_addr;
    }

    if (addr_in)
    {
        const char *rc = inet_ntop(addr->sa_family, addr_in, buf, INET6_ADDRSTRLEN);
        if (rc)
        {
            std::string remote_addr(rc);
            return remote_addr;
        }
    }

    return "[unknown address]";
}

const std::string websocketCloseCodeToString(uint16_t code)
{
    switch (code) {
    case 1000:
        return "Normal websocket close";
    case 1001:
        return "Browser navigating away from page";
    default:
        return formatString("Websocket status code %d", code);
    }
}

const std::string protocolVersionString(ProtocolVersion p)
{
    switch (p)
    {
    case ProtocolVersion::None:
        return "none";
    case ProtocolVersion::Mqtt31:
        return "3.1";
    case ProtocolVersion::Mqtt311:
        return "3.1.1";
    default:
        return "unknown";
    }
}
