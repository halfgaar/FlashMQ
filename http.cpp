#include "http.h"

#include <sstream>

#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "utils.h"
#include "exceptions.h"

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

std::string generateRedirect(const std::string &location)
{
    const std::string text("Redirecting ACME request.");

    std::ostringstream oss;
    oss << "HTTP/1.1 301 Redirect\r\n";
    oss << "Content-Type: text/plain\r\n";
    oss << "Content-Length: " << text.size() << "\r\n";
    oss << "Location: " << location << "\r\n";
    oss << "\r\n";
    oss << text;
    oss.flush();
    return oss.str();
}

std::optional<HttpRequest> parseHttpHeader(CirBuf &buf)
{
    if (buf.usedBytes() >= 16384)
    {
        return BadHttpRequest("Too much data for HTTP request");
    }

    HttpRequest::Data result;
    std::vector<std::string> lines;

    {
        bool doubleEmptyLine = false; // meaning, the HTTP header is complete

        const std::vector<char> buf_data = buf.peekAllToVector();

        const std::string beginning(buf_data.data(), std::min<size_t>(4, buf_data.size()));

        if (buf_data.size() >= 4 && beginning != "GET ")
        {
            return BadHttpRequest("HTTP request should start with GET.");
        }

        const std::string s(buf_data.data(), buf_data.size());
        std::istringstream is(s);
        for (std::string line; std::getline(is, line);)
        {
            trim(line);

            if (line.empty())
            {
                doubleEmptyLine = true;
                break;
            }

            lines.push_back(line);
        }

        if (!doubleEmptyLine)
            return {};
    }

    bool firstLine = true;

    for (const std::string &line : lines)
    {
        if (firstLine)
        {
            firstLine = false;
            if (!startsWith(line, "GET"))
                return BadHttpRequest("HTTP request should start with GET.");

            const std::vector<std::string> fields = splitToVector(line, ' ', std::numeric_limits<size_t>::max(), false);

            if (fields.size() != 3)
                return BadHttpRequest("HTTP request should include three fields.");

            result.request = fields.at(1);
            continue;
        }

        std::list<std::string> fields = split(line, ':', 1);

        if (fields.size() != 2)
        {
            return BadHttpRequest("This does not look like a HTTP request.");
        }

        const std::vector<std::string> fields2(fields.begin(), fields.end());
        std::string name = str_tolower(fields2[0]);
        trim(name);
        std::string value = fields2[1];
        trim(value);
        std::string value_lower = str_tolower(value);

        if (name == "upgrade")
        {
            std::vector<std::string> protocols = splitToVector(value_lower, ',');
            for (std::string &prot : protocols)
            {
                trim(prot);

                if (prot == "websocket")
                {
                    result.upgrade = true;
                }
            }
        }
        else if (name == "connection" && strContains(value_lower, "upgrade"))
            result.connectionUpgrade = true;
        else if (name == "sec-websocket-key")
            result.websocketKey = value;
        else if (name == "sec-websocket-version")
            result.websocketVersion = stoi(value);
        else if (name == "sec-websocket-protocol" && strContains(value_lower, "mqtt"))
        {
            std::vector<std::string> protocols = splitToVector(value, ',');

            for(std::string &prot : protocols)
            {
                trim(prot);

                // Return what is requested, which can be 'mqttv3.1' or 'mqtt', or whatever variant.
                if (strContains(str_tolower(prot), "mqtt"))
                {
                    result.subprotocol = prot;
                }
            }
        }
        else if (name == "x-real-ip" && value.length() < 64)
        {
            result.xRealIp = value;
        }
    }

    return result;
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



HttpRequest::HttpRequest(const Data &data) :
    d(data)
{

}

HttpRequest::HttpRequest(BadHttpRequest &&other) :
    e(std::move(other))
{

}

const HttpRequest::Data &HttpRequest::value()
{
    if (e)
        throw *e;

    return d;
}

HttpRequest::operator bool() const
{
    return (!e);
}
