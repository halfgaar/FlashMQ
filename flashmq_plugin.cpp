/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "flashmq_plugin.h"

#include "logger.h"
#include "threaddata.h"
#include "threadglobals.h"
#include "subscriptionstore.h"
#include "mainapp.h"
#include "utils.h"

void flashmq_logf(int level, const char *str, ...)
{
    Logger *logger = Logger::getInstance();

    va_list valist;
    va_start(valist, str);
    logger->logf(level, str, valist);
    va_end(valist);
}

void flashmq_plugin_remove_client(const std::string &clientid, bool alsoSession, ServerDisconnectReasons reasonCode)
{
    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    std::shared_ptr<Session> session = store->lockSession(clientid);

    if (session)
    {
        std::shared_ptr<Client> client = session->makeSharedClient();

        if (client)
        {
            ReasonCodes _code = static_cast<ReasonCodes>(reasonCode);
            std::shared_ptr<ThreadData> td = client->lockThreadData();

            if (td)
            {
                td->serverInitiatedDisconnect(client, _code, "Removed from plugin");
            }
        }

        if (alsoSession)
            store->removeSession(session);
    }
}

void flashmq_plugin_remove_subscription(const std::string &clientid, const std::string &topicFilter)
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

void flashmq_publish_message(const std::string &topic, const uint8_t qos, const bool retain, const std::string &payload, uint32_t expiryInterval,
                             const std::vector<std::pair<std::string, std::string>> *userProperties,
                             const std::string *responseTopic, const std::string *correlationData, const std::string *contentType)
{
    Publish pub(topic, payload, qos);
    pub.retain = retain;

    if (userProperties)
    {
        pub.constructPropertyBuilder();

        for (const std::pair<std::string, std::string> &pair : *userProperties)
        {
            std::string key = pair.first;
            std::string value = pair.second;
            pub.propertyBuilder->writeUserProperty(std::move(key), std::move(value));
        }
    }

    if (expiryInterval)
    {
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeMessageExpiryInterval(expiryInterval);
    }

    if (responseTopic)
    {
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeResponseTopic(*responseTopic);
    }

    if (correlationData)
    {
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeCorrelationData(*correlationData);
    }

    if (contentType)
    {
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeContentType(*contentType);
    }

    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();

    if (pub.retain)
    {
        store->setRetainedMessage(pub, pub.getSubtopics());
    }

    PublishCopyFactory factory(&pub);
    store->queuePacketAtSubscribers(factory, "");
}



void flashmq_get_client_address(const std::weak_ptr<Client> &client, std::string *text, FlashMQSockAddr *addr)
{
    std::shared_ptr<Client> c = client.lock();

    if (!c)
        return;

    const struct sockaddr *orgAddr = c->getAddr();

    if (text)
        *text = sockaddrToString(orgAddr);

    if (addr)
        memcpy(addr->getAddr(), orgAddr, addr->getLen());
}

void flashmq_poll_add_fd(int fd, uint32_t events, const std::weak_ptr<void> &p)
{
    ThreadData *d = ThreadGlobals::getThreadData();

    if (!d)
        return;

    d->pollExternalFd(fd, events, p);
}

void flashmq_poll_remove_fd(uint32_t fd)
{
    ThreadData *d = ThreadGlobals::getThreadData();

    if (!d)
        return;

    d->pollExternalRemove(fd);
}

sockaddr *FlashMQSockAddr::getAddr()
{
    return reinterpret_cast<struct sockaddr*>(&this->addr_in6);
}

constexpr int FlashMQSockAddr::getLen()
{
    return sizeof(struct sockaddr_in6);
}

uint32_t flashmq_add_task(std::function<void ()> f, uint32_t delay_in_ms)
{
    ThreadData *d = ThreadGlobals::getThreadData();

    if (!d)
        throw std::runtime_error("No thread data?");

    return d->addDelayedTask(f, delay_in_ms);
}

void flashmq_remove_task(uint32_t id)
{
    ThreadData *d = ThreadGlobals::getThreadData();

    if (!d)
        return;

    d->removeDelayedTask(id);
}
