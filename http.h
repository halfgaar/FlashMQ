#ifndef HTTP_H
#define HTTP_H

#include <string>

#include "cirbuf.h"
#include "types.h"

std::string generateWebsocketAcceptString(const std::string &websocketKey);
std::string generateInvalidWebsocketVersionHttpHeaders(const int wantedVersion);
std::string generateBadHttpRequestReponse(const std::string &msg);
std::string generateWebsocketAnswer(const std::string &acceptString, const std::string &subprotocol);
bool parseHttpHeader(CirBuf &buf, std::string &websocket_key, int &websocket_version, std::string &subprotocol, std::string &xRealIp);
std::string websocketCloseCodeToString(uint16_t code);
std::string protocolVersionString(ProtocolVersion p);

#endif // HTTP_H
