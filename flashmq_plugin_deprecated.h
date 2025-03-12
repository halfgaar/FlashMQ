#ifndef FLASHMQ_PLUGIN_DEPRECATED_H
#define FLASHMQ_PLUGIN_DEPRECATED_H

#include <string>

extern "C"
{

void flashmq_plugin_remove_subscription(const std::string &clientid, const std::string &topicFilter);

}

#endif // FLASHMQ_PLUGIN_DEPRECATED_H
