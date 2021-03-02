#include "utils.h"

#include "sys/time.h"
#include "sys/random.h"
#include <algorithm>
#include <cstdio>

#include "openssl/ssl.h"
#include "openssl/err.h"

#include "exceptions.h"
#include "cirbuf.h"
#include "sslctxmanager.h"
#include "logger.h"

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
        const std::vector<std::string> fields2(fields.begin(), fields.end());
        std::string name = str_tolower(fields2[0]);
        trim(name);
        std::string value = fields2[1];
        trim(value);
        std::string value_lower = str_tolower(value);

        if (name == "upgrade" && value_lower == "websocket")
            upgradeHeaderSeen = true;
        else if (name == "connection" && value_lower == "upgrade")
            connectionHeaderSeen = true;
        else if (name == "sec-websocket-key")
            websocket_key = value;
        else if (name == "sec-websocket-version")
            websocket_version = stoi(value);
    }

    if (doubleEmptyLine)
    {
        if (!connectionHeaderSeen || !upgradeHeaderSeen)
            throw BadHttpRequest("HTTP request is not a websocket upgrade request.");
    }

    return doubleEmptyLine;
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

    std::string result(buf, 512);

    return result;
}
