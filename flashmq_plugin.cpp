/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "flashmq_plugin.h"
#include "flashmq_plugin_deprecated.h"

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

/**
 * @brief flashmq_plugin_remove_client for previous plugin versions.
 */
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

void flashmq_plugin_remove_client_v4(const std::weak_ptr<Session> &session, bool alsoSession, ServerDisconnectReasons reasonCode)
{
    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    if (!store) return;
    std::shared_ptr<Session> session_locked = session.lock();
    if (!session_locked) return;
    std::shared_ptr<Client> client = session_locked->makeSharedClient();
    if (!client) return;
    std::shared_ptr<ThreadData> td = client->lockThreadData();
    if (!td) return;

    ReasonCodes _code = static_cast<ReasonCodes>(reasonCode);
    td->serverInitiatedDisconnect(client, _code, "Removed from plugin");

    if (alsoSession)
        store->removeSession(session_locked);
}

/**
 * @brief flashmq_plugin_remove_subscription for previous plugin versions.
 */
void flashmq_plugin_remove_subscription(const std::string &clientid, const std::string &topicFilter)
{
    if (!(isValidUtf8(topicFilter) && isValidSubscribePath(topicFilter)))
        throw std::runtime_error("Unsubscribing from plugin failed: invalid topic filter: " + topicFilter);

    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    std::shared_ptr<Session> session = store->lockSession(clientid);
    if (!session) return;

    std::vector<std::string> subtopics = splitTopic(topicFilter);
    std::string shareName;
    std::string _;
    parseSubscriptionShare(subtopics, shareName, _);

    store->removeSubscription(session, subtopics, shareName);
}

void flashmq_plugin_remove_subscription_v4(const std::weak_ptr<Session> &session, const std::string &topicFilter)
{
    if (!(isValidUtf8(topicFilter) && isValidSubscribePath(topicFilter)))
        throw std::runtime_error("Unsubscribing from plugin failed: invalid topic filter: " + topicFilter);

    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    std::shared_ptr<Session> session_locked = session.lock();
    if (!session_locked) return;

    std::vector<std::string> subtopics = splitTopic(topicFilter);
    std::string shareName;
    std::string _;
    parseSubscriptionShare(subtopics, shareName, _);

    store->removeSubscription(session_locked, subtopics, shareName);
}

bool flashmq_plugin_add_subscription(
    const std::weak_ptr<Session> &session, const std::string &topicFilter, uint8_t qos, bool noLocal, bool retainAsPublished,
    const uint32_t subscriptionIdentifier)
{
    if (!(isValidUtf8(topicFilter) && isValidSubscribePath(topicFilter)))
        throw std::runtime_error("Subscribing from plugin failed: invalid topic filter: " + topicFilter);

    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    if (!store) return false;
    std::shared_ptr<Session> session_locked = session.lock();
    if (!session_locked) return false;

    std::vector<std::string> subtopics = splitTopic(topicFilter);
    std::string shareName;
    std::string topicDummy;
    parseSubscriptionShare(subtopics, shareName, topicDummy);

    const AddSubscriptionType result = store->addSubscription(session_locked, subtopics, qos, noLocal, retainAsPublished, shareName, subscriptionIdentifier);
    return result == AddSubscriptionType::Invalid ? false : true;
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
        for (const std::pair<std::string, std::string> &pair : *userProperties)
        {
            pub.addUserProperty(pair.first, pair.second);
        }
    }

    if (expiryInterval)
    {
        pub.setExpireAfter(expiryInterval);
    }

    if (responseTopic)
    {
        pub.responseTopic = *responseTopic;
    }

    if (correlationData)
    {
        pub.correlationData = *correlationData;
    }

    if (contentType)
    {
        pub.contentType = *contentType;
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
    ThreadData *d = ThreadGlobals::getThreadData().get();

    if (!d)
        return;

    d->pollExternalFd(fd, events, p);
}

void flashmq_poll_remove_fd(uint32_t fd)
{
    ThreadData *d = ThreadGlobals::getThreadData().get();

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
    std::shared_ptr<ThreadData> d = ThreadGlobals::getThreadData();

    if (!d)
        throw std::runtime_error("No thread data?");

    return d->addDelayedTask(f, delay_in_ms);
}

void flashmq_remove_task(uint32_t id)
{
    std::shared_ptr<ThreadData> d = ThreadGlobals::getThreadData();

    if (!d)
        return;

    d->removeDelayedTask(id);
}

void flashmq_get_session_pointer(const std::string &clientid, const std::string &username, std::weak_ptr<Session> &sessionOut)
{
    std::shared_ptr<SubscriptionStore> store = MainApp::getMainApp()->getSubscriptionStore();
    if (!store) return;
    std::shared_ptr<Session> session = store->lockSession(clientid);
    if (!session) return;
    if (session->getUsername() != username) return;
    sessionOut = session;
}

void flashmq_get_client_pointer(const std::weak_ptr<Session> &session, std::weak_ptr<Client> &clientOut)
{
    std::shared_ptr<Session> sessionLocked = session.lock();
    if (!sessionLocked) return;
    clientOut = sessionLocked->makeSharedClient();
}
