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
#include <sys/statvfs.h>
#include <algorithm>
#include <cstring>
#include <signal.h>
#include <iomanip>
#include <time.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <signal.h>
#include <pwd.h>
#include <grp.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "exceptions.h"
#include "cirbuf.h"
#include "sslctxmanager.h"
#include "logger.h"
#include "evpencodectxmanager.h"


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

bool strContains(const std::string &s, const std::string &needle)
{
    return s.find(needle) != std::string::npos;
}

// Only necessary for tests at this point.
bool isValidUtf8Generic(const char *s, bool alsoCheckInvalidPublishChars)
{
    const std::string s2(s);
    return isValidUtf8Generic(s2, alsoCheckInvalidPublishChars);
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
    bool wildcardAllowed = true;
    bool nextMustBeSlash = false;
    bool poundSeen = false;

    for (const char c : s)
    {
        if (!wildcardAllowed && (c == '+' || c == '#'))
            return false;

        if (nextMustBeSlash && c != '/')
            return false;

        if (poundSeen)
            return false;

        wildcardAllowed = c == '/';
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
    thread_local static SimdUtils simdUtils;
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

void rtrim(std::string &s, char c)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [c](unsigned char ch) {
        return c != ch;
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

bool endsWith(const std::string &s, const std::string &ending)
{
    if (ending.size() > s.size())
        return false;
    return std::equal(ending.rbegin(), ending.rend(), s.rbegin());
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

std::vector<unsigned char> base64Decode(const std::string &s)
{
    if (s.length() % 4 != 0)
        throw std::runtime_error("Decoding invalid base64 string");

    if (s.empty())
        throw std::runtime_error("Trying to base64 decode an empty string.");

    const std::vector<unsigned char> input(s.begin(), s.end());
    std::vector<unsigned char> tmp(input.size() + 16);

    int outl = 0;
    int outl_total = 0;

    EvpEncodeCtxManager b64_ctx;
    if (EVP_DecodeUpdate(b64_ctx.ctx.get(), tmp.data(), &outl, input.data(), input.size()) < 0)
        throw std::runtime_error("Failure in EVP_DecodeUpdate()");
    outl_total += outl;
    if (EVP_DecodeFinal(b64_ctx.ctx.get(), tmp.data() + outl_total, &outl) < 0)
        throw std::runtime_error("Failure in EVP_DecodeFinal()");
    std::vector<unsigned char> result = make_vector<unsigned char>(tmp, 0, outl_total);
    return result;
}

std::string base64Encode(const unsigned char *input, const int length)
{
    const int pl = 4*((length+2)/3);
    std::vector<unsigned char> output(pl + 1);
    const int ol = EVP_EncodeBlock(output.data(), input, length);

    if (pl != ol)
        throw std::runtime_error("Base64 encode error.");

    std::string result = make_string(output, 0, ol);
    return result;
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
    int rc = vsnprintf(buf, bufsize, str.c_str(), valist);
    va_end(valist);

    if (rc < 0)
        return std::string();

    size_t len = std::min<size_t>(strlen(buf), bufsize);
    std::string result(buf, len);

    return result;
}

std::string_view dirnameOf(std::string_view path)
{
    size_t pos = path.find_last_of("\\/");
    return (std::string::npos == pos) ? "" : path.substr(0, pos);
}

size_t getFileSize(const std::string &path)
{
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    if (stat(path.c_str(), &statbuf) < 0)
        throw std::runtime_error("Can't get size of " + path);

    if (statbuf.st_size < 0)
        throw std::runtime_error("Size of " + path + " negative?");

    return statbuf.st_size;
}

uint64_t getFreeSpace(const std::string &path)
{
    struct statvfs statbuf;
    memset(&statbuf, 0, sizeof(struct statvfs));

    if (statvfs(path.c_str(), &statbuf) < 0)
        throw std::runtime_error("Can't get free space of " + path);

    const uint64_t result {statbuf.f_bsize * statbuf.f_bfree};
    return result;
}

/**
 * @brief Get socket family from addr that works with strict type aliasing.
 *
 * Disgruntled: if type aliasing rules are so strict, why is there no library
 * function to obtain the family from a sockaddr...?
 */
sa_family_t getFamilyFromSockAddr(const sockaddr *addr)
{
    if (!addr)
        return AF_UNSPEC;

    sockaddr tmp;
    std::memcpy(&tmp, addr, sizeof(tmp));

    return tmp.sa_family;
}

std::string sockaddrToString(const sockaddr *addr)
{
    if (!addr)
        return "[unknown address]";

    std::array<char, INET6_ADDRSTRLEN> buf;
    std::fill(buf.begin(), buf.end(), 0);

    const int family = getFamilyFromSockAddr(addr);
    const char *rc = nullptr;

    if (family == AF_INET)
    {
        struct sockaddr_in ipv4sockAddr;
        std::memcpy(&ipv4sockAddr, addr, sizeof(struct sockaddr_in));
        rc = inet_ntop(family, &ipv4sockAddr.sin_addr, buf.data(), buf.size());
    }
    else if (family == AF_INET6)
    {
        struct sockaddr_in6 ipv6sockAddr;
        std::memcpy(&ipv6sockAddr, addr, sizeof(struct sockaddr_in6));
        rc = inet_ntop(family, &ipv6sockAddr.sin6_addr, buf.data(), buf.size());
    }
    else if (family == AF_UNIX)
    {
        return std::string("unix");
    }

    if (rc == nullptr)
        return "[unknown address]";

    std::string remote_addr(rc);
    return remote_addr;
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
    case AuthResult::server_not_available:
        return ReasonCodes::ServerUnavailable;
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

void parseSubscriptionShare(std::vector<std::string> &subtopics, std::string &shareName, std::string &topic)
{
    if (subtopics.size() < 3)
    {
        if (subtopics.size() == 2 && subtopics[0] == "$share")
        {
            throw ProtocolError("Topic filter for shared subscription cannot be empty.", ReasonCodes::ProtocolError);
        }

        return;
    }

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

    if (!(subtopics.size() > 1 || (subtopics.size() == 1 && !subtopics[0].empty()) ))
        throw ProtocolError("The / character after a shared subscription name MUST be followed by a topic filter.", ReasonCodes::ProtocolError);

    topic.clear();

    for(const std::string &s : subtopics)
    {
        if (!topic.empty())
            topic.append("/");
        topic.append(s);
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

void unlink_if_sock(const std::string &path)
{
    if (path.empty())
        return;

    struct stat statbuf;
    std::memset(&statbuf, 0, sizeof(statbuf));
    if (lstat(path.c_str(), &statbuf) < 0)
        return;

    if ((statbuf.st_mode & S_IFMT) == S_IFSOCK)
    {
        unlink(path.c_str());
    }
}

void fmq_ensure_fail(const char *file, int line)
{
    std::cerr << "Assertion failure: " << file << ", line " << line << "." << std::endl;
    raise(SIGABRT);
}

std::optional<SysUserFields> get_pw_name(const std::string &user)
{
    struct passwd pwd;
    struct passwd *result = nullptr;
    std::memset(&pwd, 0, sizeof(pwd));
    std::vector<char> buf(16384);

    if (getpwnam_r(user.c_str(), &pwd, buf.data(), buf.size(), &result) != 0)
    {
        throw std::runtime_error("getpwnam_r error");
    }

    if (result == nullptr)
    {
        return {};
    }

    SysUserFields answer;
    answer.name = pwd.pw_name;
    answer.uid = pwd.pw_uid;
    answer.gid = pwd.pw_gid;
    return answer;
}

std::optional<SysGroupFields> get_gr_name(const std::string &group)
{
    struct group grp;
    struct group *result = nullptr;
    std::memset(&grp, 0, sizeof(grp));
    std::vector<char> buf(16384);

    if (getgrnam_r(group.c_str(), &grp, buf.data(), buf.size(), &result) != 0)
    {
        throw std::runtime_error("getgrnam_r error");
    }

    if (result == nullptr)
    {
        return {};
    }

    SysGroupFields answer;
    answer.name = group;
    answer.gid = grp.gr_gid;
    return answer;
}

std::optional<unsigned long> try_stoul(const std::string &s) noexcept
{
    try
    {
        size_t len = 0;
        unsigned long answer = std::stoul(s, &len);

        if (len != s.length())
            return {};

        return answer;
    }
    catch (std::exception &ex)
    {
        return {};
    }

    return {};
}
