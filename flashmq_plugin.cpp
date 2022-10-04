#include "flashmq_plugin.h"

#include "logger.h"
#include "threaddata.h"
#include "threadglobals.h"
#include "subscriptionstore.h"

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

void flashmq_remove_client(const std::string &clientid, bool alsoSession, ServerDisconnectReasons reasonCode)
{
    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    std::shared_ptr<Session> session = store->lockSession(clientid);

    if (session)
    {
        std::shared_ptr<Client> client = session->makeSharedClient();

        if (client)
        {
            ReasonCodes _code = static_cast<ReasonCodes>(reasonCode);
            client->serverInitiatedDisconnect(_code);
        }

        if (alsoSession)
            store->removeSession(session);
    }
}

void flashmq_remove_subscription(const std::string &clientid, const std::string &topicFilter)
{
    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    std::shared_ptr<Session> session = store->lockSession(clientid);

    if (session)
    {
        std::shared_ptr<Client> client = session->makeSharedClient();

        if (client)
        {
            store->removeSubscription(client, topicFilter);
        }
    }
}

void flashmq_continue_async_authentication(const std::weak_ptr<Client> &client, AuthResult result, const std::string &authMethod, const std::string &returnData)
{
    std::shared_ptr<Client> c = client.lock();

    if (!c)
        return;

    std::shared_ptr<ThreadData> td = c->lockThreadData();

    if (!td)
        return;

    td->queueContinuationOfAuthentication(c, result, authMethod, returnData);
}
