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

bool parseHttpHeader(CirBuf &buf, std::string &websocket_key, int &websocket_version, std::string &subprotocol, std::string &xRealIp)
{
    std::vector<char> buf_data = buf.peekAllToVector();

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

        if (name == "upgrade")
        {
            std::vector<std::string> protocols = splitToVector(value_lower, ',');
            for (std::string &prot : protocols)
            {
                trim(prot);

                if (prot == "websocket")
                {
                    upgradeHeaderSeen = true;
                }
            }
        }
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


