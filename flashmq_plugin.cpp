#include "flashmq_plugin.h"

#include "logger.h"

void flashmq_logf(int level, const char *str, ...)
{
    Logger *logger = Logger::getInstance();

    va_list valist;
    va_start(valist, str);
    logger->logf(level, str, valist);
    va_end(valist);
}

FlashMQMessage::FlashMQMessage(const std::string &topic, const std::vector<std::string> &subtopics, const char qos, const bool retain,
                               const std::vector<std::pair<std::string, std::string>> *userProperties) :
    topic(topic),
    subtopics(subtopics),
    userProperties(userProperties),
    qos(qos),
    retain(retain)
{

}
