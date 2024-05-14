/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <sys/stat.h>

#include "utils.h"

#include <sys/time.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <iomanip>
#include <time.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

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
    std::string::const_iterator start = input.begin();
    const std::string::const_iterator end = input.end();
    std::string::const_iterator sep_pos;

    while (list.size() < max && (sep_pos = std::find(start, end, sep)) != end) {
        if (start != sep_pos || keep_empty_parts)
            list.emplace_back(start, sep_pos);
        start = sep_pos + 1; // increase by length of separator
    }
    if (start != end || keep_empty_parts)
        list.emplace_back(start, end);
    return list;
}

bool isValidUtf8Generic(const std::string &s, bool alsoCheckInvalidPublishChars)
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

bool isValidShareName(const std::string &s)
{
    if (s.empty())
        return false;

    for (const char c : s)
    {
        if ((c == '#') | (c == '+') | (c == '/'))
            return false;
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

std::vector<std::string> splitTopic(const std::string &topic)
{
#ifdef __SSE4_2__
    return simdUtils.splitTopic(topic);
#else
    std::vector<std::string> output;
    output.reserve(16);
    std::string::const_iterator start = topic.begin();
    std::string::const_iterator sep_pos;

    do {
        sep_pos = std::find(start, topic.end(), '/');
        output.emplace_back(start, sep_pos);
        start = sep_pos + 1;
    } while (sep_pos != topic.end());

    return output;
#endif
}

std::vector<std::string> splitToVector(const std::string &input, const char sep, size_t max, bool keep_empty_parts)
{
    std::vector<std::string> output;
    output.reserve(16);
    std::string::const_iterator start = input.begin();
    std::string::const_iterator sep_pos;

    while (output.size() < max && (sep_pos = std::find(start, input.end(), sep)) != input.end()) {
        if (start != sep_pos || keep_empty_parts)
            output.emplace_back(start, sep_pos);
        start = sep_pos + 1; // increase by length of separator
    }
    if (start != input.end() || keep_empty_parts)
        output.emplace_back(start, input.end());

    return output;
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
    if (s.length() < needle.length())
        return false;

    size_t i;
    for (i = 0; i < needle.length(); i++)
    {
        if (s[i] != needle[i])
            return false;
    }

    return i == needle.length();
}

std::string getSecureRandomString(const ssize_t len)
{
    std::vector<uint64_t> buf(len);
    const ssize_t random_len = len * 8;
    ssize_t actual_len = -1;

    while ((actual_len = getrandom(buf.data(), random_len, 0)) < 0)
    {
        if (errno == EINTR)
            continue;

        break;
    }

    if (actual_len < 0 || actual_len != random_len)
    {
        throw std::runtime_error("Error requesting random data");
    }

    static constexpr std::string_view possibleCharacters{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrtsuvwxyz1234567890"};
    static constexpr size_t possibleCharactersCount = possibleCharacters.size();

    std::string randomString(buf.size(), '\0');
    std::transform(buf.begin(), buf.end(), randomString.begin(),
                   [&](uint64_t v){ return possibleCharacters[v % possibleCharactersCount];});
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

bool parseHttpHeader(CirBuf &buf, std::string &websocket_key, int &websocket_version, std::string &subprotocol, std::string &xRealIp)
{
    std::vector<char> buf_data(buf.usedBytes());
    buf.read(buf_data.data(), buf.usedBytes());

    const std::string s(buf_data.data(), buf_data.size());
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
        else if (name == "sec-websocket-protocol" && strContains(value_lower, "mqtt"))
        {
            std::vector<std::string> protocols = splitToVector(value, ',');

            for(std::string &prot : protocols)
            {
                trim(prot);

                // Return what is requested, which can be 'mqttv3.1' or 'mqtt', or whatever variant.
                if (strContains(str_tolower(prot), "mqtt"))
                {
                    subprotocol = prot;
                    subprotocol_seen = true;
                }
            }
        }
        else if (name == "x-real-ip" && value.length() < 64)
        {
            xRealIp = value;
        }
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

std::string generateWebsocketAnswer(const std::string &acceptString, const std::string &subprotocol)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 101 Switching Protocols\r\n";
    oss << "Upgrade: websocket\r\n";
    oss << "Connection: Upgrade\r\n";
    oss << "Sec-WebSocket-Accept: " << acceptString << "\r\n";
    oss << "Sec-WebSocket-Protocol: " << subprotocol << "\r\n";
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
    if (SSL_CTX_use_certificate_chain_file(sslCtx.get(), fullchain.c_str()) != 1)
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

void testSslVerifyLocations(const std::string &caFile, const std::string &caDir, const std::string &error)
{
    if (!caFile.empty() && getFileSize(caFile) <= 0)
        throw ConfigFileException(formatString("SSL 'ca_file' file '%s' is empty or invalid", caFile.c_str()));

    SslCtxManager sslCtx(TLS_client_method());

    const char *ca_file = caFile.empty() ? nullptr : caFile.c_str();
    const char *ca_dir = caDir.empty() ? nullptr : caDir.c_str();

    if (ca_file == nullptr && ca_dir == nullptr)
        return;

    if (SSL_CTX_load_verify_locations(sslCtx.get(), ca_file, ca_dir) != 1)
    {
        ERR_print_errors_cb(logSslError, NULL);
        throw ConfigFileException(error);
    }
}

std::string formatString(const std::string str, ...)
{
    constexpr const int bufsize = 512;

    char buf[bufsize + 1];
    buf[bufsize] = 0;

    va_list valist;
    va_start(valist, str);
    vsnprintf(buf, bufsize, str.c_str(), valist);
    va_end(valist);

    size_t len = std::min<size_t>(strlen(buf), bufsize);
    std::string result(buf, len);

    return result;
}

std::string_view dirnameOf(std::string_view path)
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

std::string sockaddrToString(const sockaddr *addr)
{
    if (!addr)
        return "[unknown address]";

    char buf[INET6_ADDRSTRLEN];
    const void *addr_in = nullptr;

    if (addr->sa_family == AF_INET)
    {
        const struct sockaddr_in *ipv4sockAddr = reinterpret_cast<const struct sockaddr_in*>(addr);
        addr_in = &ipv4sockAddr->sin_addr;
    }
    else if (addr->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 *ipv6sockAddr = reinterpret_cast<const struct sockaddr_in6*>(addr);
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

std::string websocketCloseCodeToString(uint16_t code)
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

std::string protocolVersionString(ProtocolVersion p)
{
    switch (p)
    {
    case ProtocolVersion::None:
        return "none";
    case ProtocolVersion::Mqtt31:
        return "3.1";
    case ProtocolVersion::Mqtt311:
        return "3.1.1";
    case ProtocolVersion::Mqtt5:
        return "5.0";
    default:
        return "unknown";
    }
}

/**
 * @brief Returns the edit distance between the two given strings
 *
 * This function uses the Wagner–Fischer algorithm to calculate the Levenshtein
 * distance between two strings: the total number of insertions, swaps, and
 * deletions that are needed to transform the one into the other.
 */
unsigned int distanceBetweenStrings(const std::string &stringA, const std::string &stringB)
{
    // The matrix contains the distances between the substrings.
    // You can find a description of the algorithm online.
    //
    // Roughly:
    //
    // line_a: "dog"
    // line_b: "horse"
    //
    //   -->
    //  |   | # | d | o | g
    //  V --+---+---+---+---+---+---+---
    //    # | P
    //    h |
    //    o |
    //    r | Q   X       Y
    //    s |
    //    e |             Z
    //
    // P = [0, 0] = the distance from ""    to ""       (which is 0)
    // Q = [0, 3] = the distance from ""    to "hor"    (which is 3, all inserts)
    // X = [1, 3] = the distance from "d"   to "hor"    (which is 3, 1 swap and 2 inserts)
    // Y = [3, 3] = the distance from "dog" to "hor"    (which is 2, both swaps)
    // Z = [3, 5] = the distance from "dog" to "horse"  (which is 4, two swaps and 2 inserts)
    //
    // The matrix does not have to be square, the dimensions depends on the inputs
    //
    // the position within stringA should always be referred to as x
    // the position within stringB should always be referred to as y

    using mymatrix = std::vector<std::vector<int>>;
    // +1 because we also need to store the length from the empty strings
    int width = stringA.size() + 1;
    int height = stringB.size() + 1;
    mymatrix distances(width, std::vector<int>(height));

    // We know that the distance from the substrings of line_a to ""
    // is equal to the length of the substring of line_a
    for (int x = 0; x < width; x++)
    {
        distances.at(x).at(0) = x;
    }

    // We know that the distance from "" to the substrings of line_b
    // is equal to the length of the substring of line_b
    for (int y = 0; y < height; y++)
    {
        distances.at(0).at(y) = y;
    }

    // Now all we do is to fill out the rest of the matrix, easy peasy
    // note we start at 1 because the top row and left column have already been calculated
    for (int x = 1; x < width; x++)
    {
        for (int y = 1; y < height; y++)
        {
            if (stringA.at(x - 1) == stringB.at(y - 1))
            {
                // the letters in both words are the same: we can travel from the top-left for free to the current state
                distances.at(x).at(y) = distances.at(x - 1).at(y - 1);
            }
            else
            {
                // let's calculate the different costs and pick the cheapest option
                // We use "+1" for all costs since they are all equally likely in our case
                int dinstance_with_deletion = distances.at(x).at(y - 1) + 1;
                int dinstance_with_insertion = distances.at(x - 1).at(y) + 1;
                int dinstance_with_substitution = distances.at(x - 1).at(y - 1) + 1;
                distances.at(x).at(y) = std::min({dinstance_with_deletion, dinstance_with_insertion, dinstance_with_substitution});
            }
        }
    }

    return distances.at(width - 1).at(height - 1); // the last cell contains our answer
}

uint32_t ageFromTimePoint(const std::chrono::time_point<std::chrono::steady_clock> &point)
{
    auto duration = std::chrono::steady_clock::now() - point;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    return seconds.count();
}

std::chrono::time_point<std::chrono::steady_clock> timepointFromAge(const uint32_t age)
{
    std::chrono::seconds seconds(age);
    std::chrono::time_point<std::chrono::steady_clock> newPoint = std::chrono::steady_clock::now() - seconds;
    return newPoint;
}

ReasonCodes authResultToReasonCode(AuthResult authResult)
{
    switch (authResult)
    {
    case AuthResult::success:
        return ReasonCodes::Success;
    case AuthResult::auth_method_not_supported:
        return ReasonCodes::BadAuthenticationMethod;
    case AuthResult::acl_denied:
    case AuthResult::login_denied:
        return ReasonCodes::NotAuthorized;
    case AuthResult::error:
        return ReasonCodes::UnspecifiedError;
    case AuthResult::auth_continue:
        return ReasonCodes::ContinueAuthentication;
    default:
        return ReasonCodes::UnspecifiedError;
    }
}

int maskAllSignalsCurrentThread()
{
    sigset_t set;
    sigfillset(&set);

    int r = pthread_sigmask(SIG_SETMASK, &set, NULL);
    return r;
}

void parseSubscriptionShare(std::vector<std::string> &subtopics, std::string &shareName)
{
    if (subtopics.size() < 3)
        return;

    const std::string &match = subtopics[0];

    if (match != "$share")
        return;

    const std::string _shareName = subtopics[1];

    if (!isValidShareName(_shareName))
        throw ProtocolError("Invalid character in share name", ReasonCodes::ProtocolError);

    for (int i = 0; i < 2; i++)
    {
        subtopics.erase(subtopics.begin());
    }

    shareName = _shareName;
}

std::string timestampWithMillis()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const time_t timer = std::chrono::system_clock::to_time_t(now);

    struct tm my_tm;
    memset(&my_tm, 0, sizeof(struct tm));
    struct tm *my_tm_result = localtime_r(&timer, &my_tm);

    if (!my_tm_result)
        return std::string("localtime-failed");

    std::ostringstream oss;

    oss << std::put_time(my_tm_result, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

void exceptionOnNonMqtt(const std::vector<char> &data)
{
    const std::string str(data.data(), data.size());

    std::istringstream is(str);
    bool firstLine = true;

    std::string line;
    while (std::getline(is, line))
    {
        if (firstLine)
        {
            firstLine = false;

            if (strContains(line, "HTTP"))
            {
                throw BadClientException("This looks like HTTP traffic.");
            }
        }
    }
}

/**
 * #               is 0
 * one/#           is 1
 * one/two/+/four  is 2
 * one/two/three   is 65535
 *
 */
uint16_t getFirstWildcardDepth(const std::vector<std::string> &subtopics)
{
    uint16_t result = std::numeric_limits<uint16_t>::max();
    uint16_t i = 0;

    for (const std::string &s : subtopics)
    {
        if (s == "#" || s == "+")
        {
            result = i;
            break;
        }

        i++;
    }

    return result;
}

std::string reasonCodeToString(ReasonCodes code)
{
    switch (code)
    {
    case ReasonCodes::Success:
        return "Success";
    //case ReasonCodes::GrantedQoS0:
    //    return "GrantedQoS0";
    case (ReasonCodes::GrantedQoS1):
        return "GrantedQoS1";
    case (ReasonCodes::GrantedQoS2):
        return "GrantedQoS2";
    case (ReasonCodes::DisconnectWithWill):
        return "DisconnectWithWill";
    case (ReasonCodes::NoMatchingSubscribers):
        return "NoMatchingSubscribers";
    case (ReasonCodes::NoSubscriptionExisted):
        return "NoSubscriptionExisted";
    case (ReasonCodes::ContinueAuthentication):
        return "ContinueAuthentication";
    case (ReasonCodes::ReAuthenticate):
        return "ReAuthenticate";
    case (ReasonCodes::UnspecifiedError):
        return "UnspecifiedError";
    case (ReasonCodes::MalformedPacket):
        return "MalformedPacket";
    case (ReasonCodes::ProtocolError):
        return "ProtocolError";
    case (ReasonCodes::ImplementationSpecificError):
        return "ImplementationSpecificError";
    case (ReasonCodes::UnsupportedProtocolVersion):
        return "UnsupportedProtocolVersion";
    case (ReasonCodes::ClientIdentifierNotValid):
        return "ClientIdentifierNotValid";
    case (ReasonCodes::BadUserNameOrPassword):
        return "BadUserNameOrPassword";
    case (ReasonCodes::NotAuthorized):
        return "NotAuthorized";
    case (ReasonCodes::ServerUnavailable):
        return "ServerUnavailable";
    case (ReasonCodes::ServerBusy):
        return "ServerBusy";
    case (ReasonCodes::Banned):
        return "Banned";
    case (ReasonCodes::ServerShuttingDown):
        return "ServerShuttingDown";
    case (ReasonCodes::BadAuthenticationMethod):
        return "BadAuthenticationMethod";
    case (ReasonCodes::KeepAliveTimeout):
        return "KeepAliveTimeout";
    case (ReasonCodes::SessionTakenOver):
        return "SessionTakenOver";
    case (ReasonCodes::TopicFilterInvalid):
        return "TopicFilterInvalid";
    case (ReasonCodes::TopicNameInvalid):
        return "TopicNameInvalid";
    case (ReasonCodes::PacketIdentifierInUse):
        return "PacketIdentifierInUse";
    case (ReasonCodes::PacketIdentifierNotFound):
        return "PacketIdentifierNotFound";
    case (ReasonCodes::ReceiveMaximumExceeded):
        return "ReceiveMaximumExceeded";
    case (ReasonCodes::TopicAliasInvalid):
        return "TopicAliasInvalid";
    case (ReasonCodes::PacketTooLarge):
        return "PacketTooLarge";
    case (ReasonCodes::MessageRateTooHigh):
        return "MessageRateTooHigh";
    case (ReasonCodes::QuotaExceeded):
        return "QuotaExceeded";
    case (ReasonCodes::AdministrativeAction):
        return "AdministrativeAction";
    case (ReasonCodes::PayloadFormatInvalid):
        return "PayloadFormatInvalid";
    case (ReasonCodes::RetainNotSupported):
        return "RetainNotSupported";
    case (ReasonCodes::QosNotSupported):
        return "QosNotSupported";
    case (ReasonCodes::UseAnotherServer):
        return "UseAnotherServer";
    case (ReasonCodes::ServerMoved):
        return "ServerMoved";
    case (ReasonCodes::SharedSubscriptionsNotSupported):
        return "SharedSubscriptionsNotSupported";
    case (ReasonCodes::ConnectionRateExceeded):
        return "ConnectionRateExceeded";
    case (ReasonCodes::MaximumConnectTime):
        return "MaximumConnectTime";
    case (ReasonCodes::SubscriptionIdentifiersNotSupported):
        return "SubscriptionIdentifiersNotSupported";
    case (ReasonCodes::WildcardSubscriptionsNotSupported):
        return "WildcardSubscriptionsNotSupported";
    default:
        break;
    }

    std::ostringstream oss;
    oss << static_cast<int>(code);
    return oss.str();
}

std::string packetTypeToString(PacketType ptype)
{
    switch (ptype)
    {
    case (PacketType::Reserved):
        return "Reserved";
    case (PacketType::CONNECT):
        return "CONNECT";
    case (PacketType::CONNACK):
        return "CONNACK";
    case (PacketType::PUBLISH):
        return "PUBLISH";
    case (PacketType::PUBACK):
        return "PUBACK";
    case (PacketType::PUBREC):
        return "PUBREC";
    case (PacketType::PUBREL):
        return "PUBREL";
    case (PacketType::PUBCOMP):
        return "PUBCOMP";
    case (PacketType::SUBSCRIBE):
        return "SUBSCRIBE";
    case (PacketType::SUBACK):
        return "SUBACK";
    case (PacketType::UNSUBSCRIBE):
        return "UNSUBSCRIBE";
    case (PacketType::UNSUBACK):
        return "UNSUBACK";
    case (PacketType::PINGREQ):
        return "PINGREQ";
    case (PacketType::PINGRESP):
        return "PINGRESP";
    case (PacketType::DISCONNECT):
        return "DISCONNECT";
    case (PacketType::AUTH):
        return "AUTH";
    default:
        break;
    }

    std::ostringstream oss;
    oss << static_cast<int>(ptype);
    return oss.str();
}


std::string propertyToString(Mqtt5Properties p)
{
    switch (p)
    {
    case (Mqtt5Properties::None):
        return "None";
    case (Mqtt5Properties::PayloadFormatIndicator):
        return "PayloadFormatIndicator";
    case (Mqtt5Properties::MessageExpiryInterval):
        return "MessageExpiryInterval";
    case (Mqtt5Properties::ContentType):
        return "ContentType";
    case (Mqtt5Properties::ResponseTopic):
        return "ResponseTopic";
    case (Mqtt5Properties::CorrelationData):
        return "CorrelationData";
    case (Mqtt5Properties::SubscriptionIdentifier):
        return "SubscriptionIdentifier";
    case (Mqtt5Properties::SessionExpiryInterval):
        return "SessionExpiryInterval";
    case (Mqtt5Properties::AssignedClientIdentifier):
        return "AssignedClientIdentifier";
    case (Mqtt5Properties::ServerKeepAlive):
        return "ServerKeepAlive";
    case (Mqtt5Properties::AuthenticationMethod):
        return "AuthenticationMethod";
    case (Mqtt5Properties::AuthenticationData):
        return "AuthenticationData";
    case (Mqtt5Properties::RequestProblemInformation):
        return "RequestProblemInformation";
    case (Mqtt5Properties::WillDelayInterval):
        return "WillDelayInterval";
    case (Mqtt5Properties::RequestResponseInformation):
        return "RequestResponseInformation";
    case (Mqtt5Properties::ResponseInformation):
        return "ResponseInformation";
    case (Mqtt5Properties::ServerReference):
        return "ServerReference";
    case (Mqtt5Properties::ReasonString):
        return "ReasonString";
    case (Mqtt5Properties::ReceiveMaximum):
        return "ReceiveMaximum";
    case (Mqtt5Properties::TopicAliasMaximum):
        return "TopicAliasMaximum";
    case (Mqtt5Properties::TopicAlias):
        return "TopicAlias";
    case (Mqtt5Properties::MaximumQoS):
        return "MaximumQoS";
    case (Mqtt5Properties::RetainAvailable):
        return "RetainAvailable";
    case (Mqtt5Properties::UserProperty):
        return "UserProperty";
    case (Mqtt5Properties::MaximumPacketSize):
        return "MaximumPacketSize";
    case (Mqtt5Properties::WildcardSubscriptionAvailable):
        return "WildcardSubscriptionAvailable";
    case (Mqtt5Properties::SubscriptionIdentifierAvailable):
        return "SubscriptionIdentifierAvailable";
    case (Mqtt5Properties::SharedSubscriptionAvailable):
        return "SharedSubscriptionAvailable";
    default:
        break;
    }

    std::ostringstream oss;
    oss << static_cast<int>(p);
    return oss.str();
}






