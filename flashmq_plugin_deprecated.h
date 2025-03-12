#ifndef FLASHMQ_PLUGIN_DEPRECATED_H
#define FLASHMQ_PLUGIN_DEPRECATED_H

#include <string>

#include "flashmq_plugin.h"

extern "C"
{

void flashmq_plugin_remove_client(const std::string &clientid, bool alsoSession, ServerDisconnectReasons reasonCode);
void flashmq_plugin_remove_subscription(const std::string &clientid, const std::string &topicFilter);

}

#endif // FLASHMQ_PLUGIN_DEPRECATED_H
