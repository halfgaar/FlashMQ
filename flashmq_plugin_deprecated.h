#ifndef FLASHMQ_PLUGIN_DEPRECATED_H
#define FLASHMQ_PLUGIN_DEPRECATED_H

#include <string>

#include "flashmq_public.h"

extern "C"
{

class API FlashMQSockAddr
{
    struct sockaddr_in6 addr_in6;

public:
    struct sockaddr *getAddr();
    static constexpr int getLen();
};

void API mosquitto_log_printf(int level, const char *fmt, ...);
void API flashmq_get_client_address(const std::weak_ptr<Client> &client, std::string *text, FlashMQSockAddr *addr);
void API flashmq_plugin_remove_client(const std::string &clientid, bool alsoSession, ServerDisconnectReasons reasonCode);
void API flashmq_plugin_remove_subscription(const std::string &clientid, const std::string &topicFilter);
void API flashmq_continue_async_authentication(const std::weak_ptr<Client> &client, AuthResult result, const std::string &authMethod, const std::string &returnData);

}

#endif // FLASHMQ_PLUGIN_DEPRECATED_H
