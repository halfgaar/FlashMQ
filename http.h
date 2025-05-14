#ifndef HTTP_H
#define HTTP_H

#include <string>

#include "cirbuf.h"
#include "types.h"
#include "exceptions.h"

class HttpRequest
{
public:
    struct Data
    {
        std::string request;
        bool upgrade = false;
        bool connectionUpgrade = false;
        std::optional<std::string> websocketKey;
        std::optional<int> websocketVersion;
        std::optional<std::string> subprotocol;
        std::optional<std::string> xRealIp;
    };

    HttpRequest(const Data &data);
    HttpRequest(BadHttpRequest &&other);
    const Data &value();
    operator bool() const;

private:

    std::optional<BadHttpRequest> e;
    Data d;
};



std::string generateWebsocketAcceptString(const std::string &websocketKey);
std::string generateInvalidWebsocketVersionHttpHeaders(const int wantedVersion);
std::string generateBadHttpRequestReponse(const std::string &msg);
std::string generateWebsocketAnswer(const std::string &acceptString, const std::string &subprotocol);
std::string generateRedirect(const std::string &location);
std::optional<HttpRequest> parseHttpHeader(CirBuf &buf);
std::string websocketCloseCodeToString(uint16_t code);
std::string protocolVersionString(ProtocolVersion p);

#endif // HTTP_H
