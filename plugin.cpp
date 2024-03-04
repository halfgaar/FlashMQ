/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "plugin.h"

#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <dlfcn.h>
#include <fstream>
#include <sys/stat.h>
#include <cassert>

#include "exceptions.h"
#include "unscopedlock.h"
#include "utils.h"
#include "client.h"
#include "threadglobals.h"
#include "threaddata.h"

std::mutex Authentication::initMutex;
std::mutex Authentication::deinitMutex;
std::mutex Authentication::authChecksMutex;

void mosquitto_log_printf(int level, const char *fmt, ...)
{
    Logger *logger = Logger::getInstance();
    va_list valist;
    va_start(valist, fmt);
    logger->logf(level, fmt, valist);
    va_end(valist);
}

MosquittoPasswordFileEntry::MosquittoPasswordFileEntry(PasswordHashType type, const std::vector<char> &&salt, const std::vector<char> &&cryptedPassword, int iterations) :
    type(type),
    salt(salt),
    cryptedPassword(cryptedPassword),
    iterations(iterations)
{

}


Authentication::Authentication(Settings &settings) :
    settings(settings),
    mosquittoPasswordFile(settings.mosquittoPasswordFile),
    mosquittoAclFile(settings.mosquittoAclFile),
    mosquittoDigestContext(EVP_MD_CTX_new())
{
    logger = Logger::getInstance();

    if(!sha512)
    {
        throw std::runtime_error("Failed to initialize SHA512 for decoding auth entry");
    }

    EVP_DigestInit_ex(mosquittoDigestContext, sha512, NULL);
    memset(&mosquittoPasswordFileLastLoad, 0, sizeof(struct timespec));
}

Authentication::~Authentication()
{
    EVP_MD_CTX_free(mosquittoDigestContext);
    mosquittoDigestContext = nullptr;
}

void *Authentication::loadSymbol(void *handle, const char *symbol, bool exceptionOnError) const
{
    void *r = dlsym(handle, symbol);

    if (r == NULL && exceptionOnError)
    {
        std::string errmsg(dlerror());
        throw FatalError(errmsg);
    }

    return r;
}

void Authentication::loadPlugin(const PluginLoader &l)
{
    if (!l.loaded())
        return;

    initialized = false;

    pluginFamily = l.getPluginFamily();
    flashmqPluginVersionNumber = l.getFlashMQPluginVersion();

    if (pluginFamily == PluginFamily::MosquittoV2)
    {
        init_v2 = (F_plugin_init_v2)l.loadSymbol( "mosquitto_auth_plugin_init");
        cleanup_v2 = (F_plugin_cleanup_v2)l.loadSymbol( "mosquitto_auth_plugin_cleanup");
        security_init_v2 = (F_plugin_security_init_v2)l.loadSymbol("mosquitto_auth_security_init");
        security_cleanup_v2 = (F_plugin_security_cleanup_v2)l.loadSymbol("mosquitto_auth_security_cleanup");
        acl_check_v2 = (F_plugin_acl_check_v2)l.loadSymbol("mosquitto_auth_acl_check");
        unpwd_check_v2 = (F_plugin_unpwd_check_v2)l.loadSymbol("mosquitto_auth_unpwd_check");
        psk_key_get_v2 = (F_plugin_psk_key_get_v2)l.loadSymbol("mosquitto_auth_psk_key_get");
    }
    else if (pluginFamily == PluginFamily::FlashMQ)
    {
        flashmq_plugin_allocate_thread_memory_v1 = (F_flashmq_plugin_allocate_thread_memory_v1)l.loadSymbol("flashmq_plugin_allocate_thread_memory");
        flashmq_plugin_deallocate_thread_memory_v1 = (F_flashmq_plugin_deallocate_thread_memory_v1)l.loadSymbol("flashmq_plugin_deallocate_thread_memory");
        flashmq_plugin_init_v1 = (F_flashmq_plugin_init_v1)l.loadSymbol("flashmq_plugin_init");
        flashmq_plugin_deinit_v1 = (F_flashmq_plugin_deinit_v1)l.loadSymbol("flashmq_plugin_deinit");
        flashmq_plugin_login_check_v1 = (F_flashmq_plugin_login_check_v1)l.loadSymbol("flashmq_plugin_login_check");
        flashmq_plugin_periodic_event_v1 = (F_flashmq_plugin_periodic_event_v1)l.loadSymbol("flashmq_plugin_periodic_event", false);
        flashmq_plugin_extended_auth_v1 = (F_flashmq_plugin_extended_auth_v1)l.loadSymbol("flashmq_plugin_extended_auth", false);
        flashmq_plugin_alter_subscription_v1 = (F_flashmq_plugin_alter_subscription_v1)l.loadSymbol("flashmq_plugin_alter_subscription", false);
        flashmq_plugin_alter_publish_v1 = (F_flashmq_plugin_alter_publish_v1)l.loadSymbol("flashmq_plugin_alter_publish", false);
        flashmq_plugin_client_disconnected_v1 = (F_flashmq_plugin_client_disconnected_v1)l.loadSymbol("flashmq_plugin_client_disconnected", false);
        flashmq_plugin_poll_event_received_v1 = (F_flashmq_plugin_poll_event_received_v1)l.loadSymbol("flashmq_plugin_poll_event_received", false);

        if (flashmqPluginVersionNumber == 1)
        {
            flashmq_plugin_acl_check_v1 = (F_flashmq_plugin_acl_check_v1)l.loadSymbol("flashmq_plugin_acl_check");
            flashmq_plugin_alter_publish_v1 = (F_flashmq_plugin_alter_publish_v1)l.loadSymbol("flashmq_plugin_alter_publish", false);
        }
        else
        {
            flashmq_plugin_acl_check_v2 = (F_flashmq_plugin_acl_check_v2)l.loadSymbol("flashmq_plugin_acl_check");
            flashmq_plugin_alter_publish_v2 = (F_flashmq_plugin_alter_publish_v2)l.loadSymbol("flashmq_plugin_alter_publish", false);
        }
    }
    else
    {
        throw FatalError("Unreachable error reached?");
    }

    initialized = true;
}

/**
 * @brief plugin::init is like Mosquitto's init(), and is to allow the plugin to init memory. Plugins should not load
 * their authentication data here. That's what securityInit() is for.
 */
void Authentication::init()
{
    if (pluginFamily == PluginFamily::None)
        return;

    UnscopedLock lock(initMutex);
    if (settings.pluginSerializeInit)
        lock.lock();

    if (quitting)
        return;

    if (pluginFamily == PluginFamily::MosquittoV2)
    {
        AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
        int result = init_v2(&pluginData, authOpts.head(), authOpts.size());
        if (result != 0)
            throw FatalError("Error initialising auth plugin.");
    }
    else if (pluginFamily == PluginFamily::FlashMQ)
    {
        std::unordered_map<std::string, std::string> &authOpts = settings.getFlashmqpluginOpts();
        flashmq_plugin_allocate_thread_memory_v1(&pluginData, authOpts);
    }
}

void Authentication::cleanup()
{
    if (pluginFamily == PluginFamily::None)
        return;

    logger->logf(LOG_NOTICE, "Cleaning up authentication.");

    securityCleanup(false);

    UnscopedLock lock(deinitMutex);
    if (settings.pluginSerializeInit)
        lock.lock();

    if (pluginFamily == PluginFamily::MosquittoV2)
    {
        AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
        int result = cleanup_v2(pluginData, authOpts.head(), authOpts.size());
        if (result != 0)
            logger->logf(LOG_ERR, "Error cleaning up auth plugin"); // Not doing exception, because we're shutting down anyway.
    }
    else if (pluginFamily == PluginFamily::FlashMQ)
    {
        try
        {
            std::unordered_map<std::string, std::string> &authOpts = settings.getFlashmqpluginOpts();
            flashmq_plugin_deallocate_thread_memory_v1(pluginData, authOpts);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Error cleaning up auth plugin: '%s'", ex.what()); // Not doing exception, because we're shutting down anyway.
        }
    }
}

/**
 * @brief plugin::securityInit initializes the security data, like loading users, ACL tables, etc.
 * @param reloading
 */
void Authentication::securityInit(bool reloading)
{
    if (pluginFamily == PluginFamily::None)
        return;

    UnscopedLock lock(initMutex);
    if (settings.pluginSerializeInit)
        lock.lock();

    if (quitting)
        return;

    if (pluginFamily == PluginFamily::MosquittoV2)
    {
        AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
        int result = security_init_v2(pluginData, authOpts.head(), authOpts.size(), reloading);
        if (result != 0)
        {
            throw pluginException("Plugin function mosquitto_auth_security_init returned an error. If it didn't log anything, we don't know what it was.");
        }
    }
    else if (pluginFamily == PluginFamily::FlashMQ)
    {
        // The exception handling is higher up in the call stack, because it needs to be different on first start vs reload.
        std::unordered_map<std::string, std::string> &authOpts = settings.getFlashmqpluginOpts();
        flashmq_plugin_init_v1(pluginData, authOpts, reloading);
    }

    initialized = true;

    periodicEvent();
}

void Authentication::securityCleanup(bool reloading)
{
    if (pluginFamily == PluginFamily::None)
        return;

    initialized = false;

    UnscopedLock lock(deinitMutex);
    if (settings.pluginSerializeInit)
        lock.lock();

    if (pluginFamily == PluginFamily::MosquittoV2)
    {
        AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
        int result = security_cleanup_v2(pluginData, authOpts.head(), authOpts.size(), reloading);

        if (result != 0)
        {
            throw pluginException("Plugin function mosquitto_auth_security_cleanup returned an error. If it didn't log anything, we don't know what it was.");
        }
    }
    else if (pluginFamily == PluginFamily::FlashMQ)
    {
        // The exception handling is higher up in the call stack, because it needs to be different on first start vs reload.
        std::unordered_map<std::string, std::string> &authOpts = settings.getFlashmqpluginOpts();
        flashmq_plugin_deinit_v1(pluginData, authOpts, reloading);
    }
}

/**
 * @brief Authentication::aclCheck performs a write ACL check on the incoming publish.
 * @param publishData
 * @return
 *
 * Internal publishes write (publish) access is always allowed (it makes little sense that a plugin would have to explicitly allow
 * those), but they are passed through the plugin, so you can inspect them. The read access can still be rejected by a plugin.
 */
AuthResult Authentication::aclCheck(Publish &publishData, std::string_view payload, AclAccess access)
{
    AuthResult result = aclCheck(publishData.client_id, publishData.username, publishData.topic, publishData.getSubtopics(), payload, access, publishData.qos,
                                 publishData.retain, publishData.getUserProperties());

    // Anonymous publishes come from FlashMQ internally, like SYS topics. We need to allow them.
    if (access == AclAccess::write && publishData.client_id.empty())
        result = AuthResult::success;

    return result;
}

AuthResult Authentication::aclCheck(const std::string &clientid, const std::string &username, const std::string &topic, const std::vector<std::string> &subtopics,
                                    std::string_view payload, AclAccess access, uint8_t qos, bool retain,
                                    const std::vector<std::pair<std::string, std::string>> *userProperties)
{
    assert(subtopics.size() > 0);
#ifdef TESTING
    // I could technically test with empty payload, but so far I don't, and this is a good way for now to check I don't miss the payload
    // because of the payload copy prevention optimization.
    assert(retain || access == AclAccess::subscribe || !payload.empty());
#endif

    ThreadData *threadData = ThreadGlobals::getThreadData();

    switch (access)
    {
    case AclAccess::read:
        threadData->aclReadChecks.inc(1);
        break;
    case AclAccess::write:
        threadData->aclWriteChecks.inc(1);
        break;
    case AclAccess::subscribe:
        threadData->aclSubscribeChecks.inc(1);
        break;
    case AclAccess::register_will:
        threadData->aclRegisterWillChecks.inc(1);
        break;
    default:
        break;
    }

    AuthResult firstResult = aclCheckFromMosquittoAclFile(clientid, username, subtopics, access);

    if (firstResult != AuthResult::success)
        return firstResult;

    if (pluginFamily == PluginFamily::None)
        return firstResult;

    if (!initialized)
    {
        logger->logf(LOG_ERR, "ACL check by plugin wanted, but initialization failed.  Can't perform check.");
        return AuthResult::error;
    }

    UnscopedLock lock(authChecksMutex);
    if (settings.pluginSerializeAuthChecks)
        lock.lock();

    if (pluginFamily == PluginFamily::MosquittoV2)
    {
        // We have to do this, because Mosquitto plugin v2 has no notion of checking subscribes.
        if (access == AclAccess::subscribe)
            return AuthResult::success;

        // We have to do this, because Mosquitto plugins has no notion of will registration ACL.
        if (access == AclAccess::register_will)
            return AuthResult::success;

        int result = acl_check_v2(pluginData, clientid.c_str(), username.c_str(), topic.c_str(), static_cast<int>(access));
        AuthResult result_ = static_cast<AuthResult>(result);

        if (result_ == AuthResult::error)
        {
            logger->logf(LOG_ERR, "ACL check by plugin returned error for topic '%s'. If it didn't log anything, we don't know what it was.", topic.c_str());
        }

        return result_;
    }
    else if (pluginFamily == PluginFamily::FlashMQ)
    {
        // I'm using this try/catch because propagating the exception higher up conflicts with who gets the blame, and then the publisher
        // gets disconnected.
        try
        {
            if (flashmqPluginVersionNumber == 1)
                return flashmq_plugin_acl_check_v1(pluginData, access, clientid, username, topic, subtopics, qos, retain, userProperties);
            else
                return flashmq_plugin_acl_check_v2(pluginData, access, clientid, username, topic, subtopics, payload, qos, retain, userProperties);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Error doing ACL check in plugin: '%s'", ex.what());
            logger->logf(LOG_WARNING, "Throwing exceptions from auth plugin login/ACL checks is slow. There's no need.");
        }
    }

    return AuthResult::error;
}

AuthResult Authentication::unPwdCheck(const std::string &clientid, const std::string &username, const std::string &password,
                                      const std::vector<std::pair<std::string, std::string>> *userProperties, const std::weak_ptr<Client> &client,
                                      const bool allowAnonymous)
{
    /*
     * This first construct is designed so that even when you allow anonymous, user verification still works, with the password file or
     * plugin. If login is denied based on a password file, the attempt is still given to the plugin. However, when the auth succeeds
     * based on the password file, it's not given to the plugin anymore.
     *
     * Also, when you allow anonymous, loging in with a non-existing user will work. But, when the user does exist, it must match.
     */

    AuthResult firstResult = allowAnonymous ? AuthResult::success : AuthResult::login_denied;

    if (!this->mosquittoPasswordFile.empty())
    {
        const std::optional<AuthResult> r = unPwdCheckFromMosquittoPasswordFile(username, password);

        if (r)
            firstResult = r.value();

        if (firstResult == AuthResult::success)
            return firstResult;
    }

    if (pluginFamily == PluginFamily::None)
        return firstResult;

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Username+password check with plugin wanted, but initialization failed. Can't perform check.");
        return AuthResult::error;
    }

    UnscopedLock lock(authChecksMutex);
    if (settings.pluginSerializeAuthChecks)
        lock.lock();

    if (pluginFamily == PluginFamily::MosquittoV2)
    {
        int result = unpwd_check_v2(pluginData, username.c_str(), password.c_str());
        AuthResult r = static_cast<AuthResult>(result);

        if (r == AuthResult::error)
        {
            logger->logf(LOG_ERR, "Username+password check by plugin returned error for user '%s'. If it didn't log anything, we don't know what it was.", username.c_str());
        }

        return r;
    }
    else if (pluginFamily == PluginFamily::FlashMQ)
    {
        // I'm using this try/catch because propagating the exception higher up conflicts with who gets the blame, and then the publisher
        // gets disconnected.
        try
        {
            return flashmq_plugin_login_check_v1(pluginData, clientid, username, password, userProperties, client);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Error doing login check in plugin: '%s'", ex.what());
            logger->logf(LOG_WARNING, "Throwing exceptions from auth plugin login/ACL checks is slow. There's no need.");
        }
    }

    return AuthResult::error;
}

AuthResult Authentication::extendedAuth(const std::string &clientid, ExtendedAuthStage stage, const std::string &authMethod,
                                        const std::string &authData, const std::vector<std::pair<std::string, std::string>> *userProperties,
                                        std::string &returnData, std::string &username, const std::weak_ptr<Client> &client)
{
    if (pluginFamily == PluginFamily::None)
        return AuthResult::auth_method_not_supported;

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Extended auth check with plugin wanted, but initialization failed. Can't perform check.");
        return AuthResult::error;
    }

    UnscopedLock lock(authChecksMutex);
    if (settings.pluginSerializeAuthChecks)
        lock.lock();

    if (pluginFamily == PluginFamily::FlashMQ)
    {
        if (!flashmq_plugin_extended_auth_v1)
            return AuthResult::auth_method_not_supported;

        // I'm using this try/catch because propagating the exception higher up conflicts with who gets the blame, and then the publisher
        // gets disconnected.
        try
        {
            return flashmq_plugin_extended_auth_v1(pluginData, clientid, stage, authMethod, authData, userProperties, returnData, username, client);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Error doing login check in plugin: '%s'", ex.what());
            logger->logf(LOG_WARNING, "Throwing exceptions from auth plugin login/ACL checks is slow. There's no need.");
        }
    }
    else if (pluginFamily == PluginFamily::MosquittoV2)
    {
        throw ProtocolError("Mosquitto v2 plugin doesn't support extended auth.", ReasonCodes::BadAuthenticationMethod);
    }

    return AuthResult::error;
}

bool Authentication::alterSubscribe(const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics, uint8_t &qos,
                                    const std::vector<std::pair<std::string, std::string>> *userProperties)
{
    if (pluginFamily == PluginFamily::None)
    {
        return false;
    }

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Plugin alterSubscribe called, but initialization failed or not performed.");
        return false;
    }

    if (pluginFamily == PluginFamily::FlashMQ && flashmq_plugin_alter_subscription_v1)
    {
        try
        {
            return flashmq_plugin_alter_subscription_v1(pluginData, clientid, topic, subtopics, qos, userProperties);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Exception in 'flashmq_plugin_alter_subscription': '%s'. You now have undefined behavior.", ex.what());
        }
    }

    return false;
}

bool Authentication::alterPublish(const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics, std::string_view payload,
                                  uint8_t &qos, bool &retain, std::vector<std::pair<std::string, std::string>> *userProperties)
{
#ifdef TESTING
    // I could technically test with empty payload, but so far I don't, and this is a good way for now to check I don't miss the payload
    // because of the payload copy prevention optimization.
    assert(retain || !payload.empty());
#endif

    if (pluginFamily == PluginFamily::None)
    {
        return false;
    }

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Plugin alterPublish called, but initialization failed or not performed.");
        return false;
    }

    if (pluginFamily == PluginFamily::FlashMQ && flashmq_plugin_alter_publish_v1)
    {
        try
        {
            if (flashmqPluginVersionNumber == 1)
                return flashmq_plugin_alter_publish_v1(pluginData, clientid, topic, subtopics, qos, retain, userProperties);
            else
                return flashmq_plugin_alter_publish_v2(pluginData, clientid, topic, subtopics, payload, qos, retain, userProperties);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Exception in 'flashmq_plugin_alter_publish': '%s'. You now have undefined behavior.", ex.what());
        }
    }

    return false;
}

void Authentication::clientDisconnected(const std::string &clientid)
{
    if (pluginFamily == PluginFamily::None)
    {
        return;
    }

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Plugin clientDisconnected called, but initialization failed or not performed.");
        return;
    }

    if (pluginFamily == PluginFamily::FlashMQ && flashmq_plugin_client_disconnected_v1)
    {
        try
        {
            flashmq_plugin_client_disconnected_v1(pluginData, clientid);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Exception in 'flashmq_plugin_client_disconnected': '%s'.", ex.what());
        }
    }
}

void Authentication::fdReady(int fd, int events, const std::weak_ptr<void> &p)
{
    if (pluginFamily == PluginFamily::None)
    {
        return;
    }

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Plugin fdReady called, but initialization failed or not performed.");
        return;
    }

    if (pluginFamily == PluginFamily::FlashMQ && flashmq_plugin_poll_event_received_v1)
    {
        try
        {
            flashmq_plugin_poll_event_received_v1(pluginData, fd, events, p);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "In 'flashmq_plugin_poll_event_v1(): '", ex.what());
        }
    }
}

void Authentication::setQuitting()
{
    this->quitting = true;
}

/**
 * @brief Authentication::loadMosquittoPasswordFile is called once on startup, and on a frequent interval, and reloads the file if changed.
 */
void Authentication::loadMosquittoPasswordFile()
{
    if (this->mosquittoPasswordFile.empty())
        return;

    if (access(this->mosquittoPasswordFile.c_str(), R_OK) != 0)
    {
        logger->logf(LOG_ERR, "Passwd file '%s' is not there or not readable.", this->mosquittoPasswordFile.c_str());
        return;
    }

    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    check<std::runtime_error>(stat(mosquittoPasswordFile.c_str(), &statbuf));
    struct timespec ctime = statbuf.st_ctim;

    if (ctime.tv_sec == this->mosquittoPasswordFileLastLoad.tv_sec)
        return;

    logger->logf(LOG_NOTICE, "Change detected in '%s'. Reloading.", this->mosquittoPasswordFile.c_str());

    try
    {
        std::ifstream infile(this->mosquittoPasswordFile, std::ios::in);
        std::unique_ptr<std::unordered_map<std::string, MosquittoPasswordFileEntry>> passwordEntries_tmp =
                std::make_unique<std::unordered_map<std::string, MosquittoPasswordFileEntry>>();

        for(std::string line; getline(infile, line ); )
        {
            trim(line);

            if (line.empty())
                continue;

            try
            {
                std::vector<std::string> fields = splitToVector(line, ':');

                if (fields.size() != 2)
                    throw std::runtime_error(formatString("Passwd file line '%s' contains more than one ':'", line.c_str()));

                const std::string &username = fields[0];

                for (const std::string &field : fields)
                {
                    if (field.size() == 0)
                    {
                        throw std::runtime_error(formatString("An empty field was found in '%'", line.c_str()));
                    }
                }

                std::vector<std::string> fields2 = splitToVector(fields[1], '$', 4, false);

                int iterations = -1;
                int saltField = -1;
                int hashField = -1;
                PasswordHashType type = PasswordHashType::SHA512;

                if (fields2[0] == "6")
                {
                    if (fields2.size() != 3)
                        throw std::runtime_error(formatString("Invalid line format in '%s'. Expected three fields separated by '$'", line.c_str()));

                    type = PasswordHashType::SHA512;
                    saltField = 1;
                    hashField = 2;
                }
                else if (fields2[0] == "7")
                {
                    if (fields2.size() != 4)
                        throw std::runtime_error(formatString("Invalid line format in '%s'. Expected four fields separated by '$'", line.c_str()));

                    type = PasswordHashType::SHA512_pbkdf2;
                    iterations = std::stoi(fields2[1]);
                    saltField = 2;
                    hashField = 3;
                }
                else
                {
                    throw std::runtime_error("Password fields must start with $6$ or $7$");
                }

                std::vector<char> salt = base64Decode(fields2[saltField]);
                std::vector<char> cryptedPassword = base64Decode(fields2[hashField]);
                passwordEntries_tmp->emplace(username, MosquittoPasswordFileEntry(type, std::move(salt), std::move(cryptedPassword), iterations));
            }
            catch (std::exception &ex)
            {
                std::string lineCut = formatString("%s...", line.substr(0, 20).c_str());
                logger->logf(LOG_ERR, "Dropping invalid username/password line: '%s'. Error: %s", lineCut.c_str(), ex.what());
            }
        }

        this->mosquittoPasswordEntries = std::move(passwordEntries_tmp);
        this->mosquittoPasswordFileLastLoad = ctime;
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error loading Mosquitto password file: '%s'. Authentication won't work.", ex.what());
    }
}

void Authentication::loadMosquittoAclFile()
{
    if (this->mosquittoAclFile.empty())
        return;

    if (access(this->mosquittoAclFile.c_str(), R_OK) != 0)
    {
        logger->logf(LOG_ERR, "ACL file '%s' is not there or not readable.", this->mosquittoAclFile.c_str());
        return;
    }

    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    check<std::runtime_error>(stat(mosquittoAclFile.c_str(), &statbuf));
    struct timespec ctime = statbuf.st_ctim;

    if (ctime.tv_sec == this->mosquittoAclFileLastChange.tv_sec)
        return;

    logger->logf(LOG_NOTICE, "Change detected in '%s'. Reloading.", this->mosquittoAclFile.c_str());

    AclTree newTree;

    // Not doing by-line error handling, because ingoring one invalid line can completely change the user's intent.
    try
    {
        std::string currentUser;

        std::ifstream infile(this->mosquittoAclFile, std::ios::in);
        for(std::string line; getline(infile, line ); )
        {
            trim(line);

            if (line.empty() || startsWith(line, "#"))
                continue;

            const std::vector<std::string> fields = splitToVector(line, ' ', 3, false);

            if (fields.size() < 2)
                throw ConfigFileException(formatString("Line does not have enough fields: %s", line.c_str()));

            const std::string &firstWord = str_tolower(fields[0]);

            if (firstWord == "topic" || firstWord == "pattern")
            {
                AclGrant g = AclGrant::ReadWrite;
                std::string topic;

                if (fields.size() == 3)
                {
                    topic = fields[2];
                    g = stringToAclGrant(fields[1]);
                }
                else if (fields.size() == 2)
                {
                    topic = fields[1];
                }
                else
                    throw ConfigFileException(formatString("Invalid markup of 'topic' line: %s", line.c_str()));

                if (!isValidSubscribePath(topic))
                    throw ConfigFileException(formatString("Topic '%s' is not a valid ACL topic", topic.c_str()));

                AclTopicType type = firstWord == "pattern" ? AclTopicType::Patterns : AclTopicType::Strings;
                newTree.addTopic(topic, g, type, currentUser);
            }
            else if (firstWord == "user")
            {
                currentUser = fields[1];
            }
            else
            {
                throw ConfigFileException(formatString("Invalid keyword '%s' in '%s'", firstWord.c_str(), line.c_str()));
            }

        }

        aclTree = std::move(newTree);
    }
    catch (std::exception &ex)
    {
        logger->logf(LOG_ERR, "Error loading Mosquitto ACL file: '%s'. Authorization won't work.", ex.what());
    }

    mosquittoAclFileLastChange = ctime;
}

AuthResult Authentication::aclCheckFromMosquittoAclFile(const std::string &clientid, const std::string &username, const std::vector<std::string> &subtopics, AclAccess access)
{
    assert(access != AclAccess::none);

    if (this->mosquittoAclFile.empty())
        return AuthResult::success;

    // We have to do this because the Mosquitto ACL file has no notion of checking subscribes.
    if (access == AclAccess::subscribe)
        return AuthResult::success;

    AclGrant ag = access == AclAccess::write ? AclGrant::Write : AclGrant::Read;
    AuthResult result = aclTree.findPermission(subtopics, ag, username, clientid);
    return result;
}

std::optional<AuthResult> Authentication::unPwdCheckFromMosquittoPasswordFile(const std::string &username, const std::string &password)
{
    if (!this->mosquittoPasswordEntries)
        return AuthResult::login_denied;

    std::optional<AuthResult> result;

    auto it = mosquittoPasswordEntries->find(username);
    if (it != mosquittoPasswordEntries->end())
    {
        result = AuthResult::login_denied;

        const MosquittoPasswordFileEntry &entry = it->second;

        if (entry.type == PasswordHashType::SHA512)
        {

            unsigned char md_value[EVP_MAX_MD_SIZE];
            unsigned int output_len = 0;

            EVP_MD_CTX_reset(mosquittoDigestContext);
            EVP_DigestInit_ex(mosquittoDigestContext, sha512, NULL);
            EVP_DigestUpdate(mosquittoDigestContext, password.c_str(), password.length());
            EVP_DigestUpdate(mosquittoDigestContext, entry.salt.data(), entry.salt.size());
            EVP_DigestFinal_ex(mosquittoDigestContext, md_value, &output_len);

            std::vector<char> hashedSalted(output_len);
            std::memcpy(hashedSalted.data(), md_value, output_len);

            if (hashedSalted == entry.cryptedPassword)
                result = AuthResult::success;
        }
        else if (entry.type == PasswordHashType::SHA512_pbkdf2)
        {
            unsigned char md_value[EVP_MAX_MD_SIZE];

            const unsigned char *saltData = reinterpret_cast<const unsigned char*>(entry.salt.data());

            PKCS5_PBKDF2_HMAC(password.c_str(), password.size(), saltData, entry.salt.size(), entry.iterations, sha512, EVP_MAX_MD_SIZE, md_value);

            std::vector<char> derivedKey(EVP_MAX_MD_SIZE);
            std::memcpy(derivedKey.data(), md_value, EVP_MAX_MD_SIZE);

            if (derivedKey == entry.cryptedPassword)
                result = AuthResult::success;
        }
    }

    return result;
}

void Authentication::periodicEvent()
{
    if (pluginFamily == PluginFamily::None)
        return;

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Auth plugin period event called, but initialization failed or not performed.");
        return;
    }

    if (pluginFamily == PluginFamily::FlashMQ && flashmq_plugin_periodic_event_v1)
    {
        try
        {
            flashmq_plugin_periodic_event_v1(pluginData);
        }
        catch (std::exception &ex)
        {
            logger->logf(LOG_ERR, "Exception in 'flashmq_plugin_periodic_event': '%s'.", ex.what());
        }
    }
}

std::string AuthResultToString(AuthResult r)
{
    if (r == AuthResult::success)
        return "success";
    if (r == AuthResult::acl_denied)
        return "ACL denied";
    if (r == AuthResult::login_denied)
        return "login Denied";
    if (r == AuthResult::error)
        return "error in check";

    return "";
}
