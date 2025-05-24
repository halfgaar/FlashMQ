#ifndef FLASHMQ_PLUGIN_DEPRECATED_H
#define FLASHMQ_PLUGIN_DEPRECATED_H

#include <string>

#include "flashmq_plugin.h"

extern "C"
{

class FlashMQSockAddr
{
    struct sockaddr_in6 addr_in6;

public:
    struct sockaddr *getAddr();
    static constexpr int getLen();
};

void flashmq_get_client_address(const std::weak_ptr<Client> &client, std::string *text, FlashMQSockAddr *addr);
void flashmq_plugin_remove_client(const std::string &clientid, bool alsoSession, ServerDisconnectReasons reasonCode);
void flashmq_plugin_remove_subscription(const std::string &clientid, const std::string &topicFilter);

}

#endif // FLASHMQ_PLUGIN_DEPRECATED_H
