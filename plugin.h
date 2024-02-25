/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef PLUGIN_H
#define PLUGIN_H

#include <string>
#include <string_view>
#include <cstring>
#include <optional>

#include "logger.h"
#include "acltree.h"
#include "flashmq_plugin.h"
#include "pluginloader.h"
#include "settings.h"
#include "types.h"

enum class PasswordHashType
{
    SHA512,
    SHA512_pbkdf2
};

/**
 * @brief The MosquittoPasswordFileEntry struct stores the decoded base64 password salt and hash.
 *
 * The Mosquitto encrypted format looks like that of crypt(2), but it's not. These are example entries:
 *
 * one:$6$emTXKCHfxMnZLDWg$gDcJRPojvOX8l7W/DRhSPoxV3CgPfECJVGRzw2Sqjdc2KIQ/CVLS1mNEuZUsp/vLdj7RCuqXCkgG43+XIc8WBA==
 * two:$7$101$twKcRmS7qxdZtFZiU+yLZHAIRNsm8deqMG9nN44pagg8t5wkUxtyWiNgbUF38cHzmgDja...VPMaNLw==
 *
 * $ is the seperator. '6' or '7' is the algorithm.
 */
struct MosquittoPasswordFileEntry
{
    PasswordHashType type;
    std::vector<char> salt;
    std::vector<char> cryptedPassword;
    int iterations = 0;

    MosquittoPasswordFileEntry(PasswordHashType type, const std::vector<char> &&salt, const std::vector<char> &&cryptedPassword, int iterations);

    // The plan was that objects of this type wouldn't be copied, but I can't get emplacing to work without it...?
    //MosquittoPasswordFileEntry(const MosquittoPasswordFileEntry &other) = delete;
};

// Mosquitto functions
typedef int (*F_plugin_init_v2)(void **, struct mosquitto_auth_opt *, int);
typedef int (*F_plugin_cleanup_v2)(void *, struct mosquitto_auth_opt *, int);
typedef int (*F_plugin_security_init_v2)(void *, struct mosquitto_auth_opt *, int, bool);
typedef int (*F_plugin_security_cleanup_v2)(void *, struct mosquitto_auth_opt *, int, bool);
typedef int (*F_plugin_acl_check_v2)(void *, const char *, const char *, const char *, int);
typedef int (*F_plugin_unpwd_check_v2)(void *, const char *, const char *);
typedef int (*F_plugin_psk_key_get_v2)(void *, const char *, const char *, char *, int);


typedef void(*F_flashmq_plugin_allocate_thread_memory_v1)(void **thread_data, std::unordered_map<std::string, std::string> &auth_opts);
typedef void(*F_flashmq_plugin_deallocate_thread_memory_v1)(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts);
typedef void(*F_flashmq_plugin_init_v1)(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts, bool reloading);
typedef void(*F_flashmq_plugin_deinit_v1)(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts, bool reloading);
typedef AuthResult(*F_flashmq_plugin_acl_check_v1)(void *thread_data, const AclAccess access, const std::string &clientid, const std::string &username,
                                                   const std::string &topic, const std::vector<std::string> &subtopics, const uint8_t qos, const bool retain,
                                                   const std::vector<std::pair<std::string, std::string>> *userProperties);
typedef AuthResult(*F_flashmq_plugin_login_check_v1)(void *thread_data, const std::string &clientid, const std::string &username, const std::string &password,
                                                          const std::vector<std::pair<std::string, std::string>> *userProperties, const std::weak_ptr<Client> &client);
typedef void (*F_flashmq_plugin_periodic_event_v1)(void *thread_data);
typedef AuthResult(*F_flashmq_plugin_extended_auth_v1)(void *thread_data, const std::string &clientid, ExtendedAuthStage stage, const std::string &authMethod,
                                                            const std::string &authData, const std::vector<std::pair<std::string, std::string>> *userProperties,
                                                            std::string &returnData, std::string &username, const std::weak_ptr<Client> &client);
typedef bool (*F_flashmq_plugin_alter_subscription_v1)(void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
                                                       uint8_t &qos, const std::vector<std::pair<std::string, std::string>> *userProperties);
typedef bool (*F_flashmq_plugin_alter_publish_v1)(void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
                                                  uint8_t &qos, bool &retain, std::vector<std::pair<std::string, std::string>> *userProperties);

typedef void (*F_flashmq_plugin_client_disconnected_v1)(void *thread_data, const std::string &clientid);
typedef void (*F_flashmq_plugin_poll_event_received_v1)(void *thread_data, int fd, int events, const std::weak_ptr<void> &p);


typedef AuthResult(*F_flashmq_plugin_acl_check_v2)(void *thread_data, const AclAccess access, const std::string &clientid, const std::string &username,
                                                   const std::string &topic, const std::vector<std::string> &subtopics, std::string_view payload,
                                                   const uint8_t qos, const bool retain, const std::vector<std::pair<std::string, std::string>> *userProperties);
typedef bool (*F_flashmq_plugin_alter_publish_v2)(void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
                                                  std::string_view payload, uint8_t &qos, bool &retain, std::vector<std::pair<std::string, std::string>> *userProperties);

extern "C"
{
    // Gets called by the plugin, so it needs to exist, globally
    void mosquitto_log_printf(int level, const char *fmt, ...);
}

std::string AuthResultToString(AuthResult r);

/**
 * @brief The Authentication class handles our integrated authentication, but also the FlashMQ and Mosquitto plugin interfaces.
 *
 * It's a bit of a legacy that both plugin handling and auth are in a class called 'Authentication', but oh well...
 */
class Authentication
{
    // Mosquitto functions
    F_plugin_init_v2 init_v2 = nullptr;
    F_plugin_cleanup_v2 cleanup_v2 = nullptr;
    F_plugin_security_init_v2 security_init_v2 = nullptr;
    F_plugin_security_cleanup_v2 security_cleanup_v2 = nullptr;
    F_plugin_acl_check_v2 acl_check_v2 = nullptr;
    F_plugin_unpwd_check_v2 unpwd_check_v2 = nullptr;
    F_plugin_psk_key_get_v2 psk_key_get_v2 = nullptr;

    F_flashmq_plugin_allocate_thread_memory_v1 flashmq_plugin_allocate_thread_memory_v1 = nullptr;
    F_flashmq_plugin_deallocate_thread_memory_v1 flashmq_plugin_deallocate_thread_memory_v1 = nullptr;
    F_flashmq_plugin_init_v1 flashmq_plugin_init_v1 = nullptr;
    F_flashmq_plugin_deinit_v1 flashmq_plugin_deinit_v1 = nullptr;
    F_flashmq_plugin_acl_check_v1 flashmq_plugin_acl_check_v1 = nullptr;
    F_flashmq_plugin_login_check_v1 flashmq_plugin_login_check_v1 = nullptr;
    F_flashmq_plugin_periodic_event_v1 flashmq_plugin_periodic_event_v1 = nullptr;
    F_flashmq_plugin_extended_auth_v1 flashmq_plugin_extended_auth_v1 = nullptr;
    F_flashmq_plugin_alter_subscription_v1 flashmq_plugin_alter_subscription_v1 = nullptr;
    F_flashmq_plugin_alter_publish_v1 flashmq_plugin_alter_publish_v1 = nullptr;
    F_flashmq_plugin_client_disconnected_v1 flashmq_plugin_client_disconnected_v1 = nullptr;
    F_flashmq_plugin_poll_event_received_v1 flashmq_plugin_poll_event_received_v1 = nullptr;

    F_flashmq_plugin_acl_check_v2 flashmq_plugin_acl_check_v2 = nullptr;
    F_flashmq_plugin_alter_publish_v2 flashmq_plugin_alter_publish_v2 = nullptr;

    static std::mutex initMutex;
    static std::mutex deinitMutex;
    static std::mutex authChecksMutex;

    PluginFamily pluginFamily = PluginFamily::None;
    int flashmqPluginVersionNumber = 0;

    Settings &settings; // A ref because I want it to always be the same as the thread's settings

    void *pluginData = nullptr;
    Logger *logger = nullptr;
    bool initialized = false;
    bool quitting = false;

    /**
     * @brief mosquittoPasswordFile is a once set value based on config. It's not reloaded on reload signal currently, because it
     * forces some decisions when you change files or remove the config option. For instance, do you remove all accounts loaded
     * from the previous one? Perhaps I'm overthinking it.
     *
     * Its content is, however, reloaded every two seconds.
     */
    const std::string mosquittoPasswordFile;
    const std::string mosquittoAclFile;

    struct timespec mosquittoPasswordFileLastLoad;
    struct timespec mosquittoAclFileLastChange;

    std::unique_ptr<std::unordered_map<std::string, MosquittoPasswordFileEntry>> mosquittoPasswordEntries;
    EVP_MD_CTX *mosquittoDigestContext = nullptr;
    const EVP_MD *sha512 = EVP_sha512();

    AclTree aclTree;

    void *loadSymbol(void *handle, const char *symbol, bool exceptionOnError = true) const;
public:
    Authentication(Settings &settings);
    Authentication(const Authentication &other) = delete;
    Authentication(Authentication &&other) = delete;
    ~Authentication();

    void loadPlugin(const PluginLoader &l);
    void init();
    void cleanup();
    void securityInit(bool reloading);
    void securityCleanup(bool reloading);
    AuthResult aclCheck(Publish &publishData, std::string_view payload, AclAccess access = AclAccess::write);
    AuthResult aclCheck(const std::string &clientid, const std::string &username, const std::string &topic, const std::vector<std::string> &subtopics,
                        std::string_view payload, AclAccess access, uint8_t qos, bool retain, const std::vector<std::pair<std::string, std::string>> *userProperties);
    AuthResult unPwdCheck(const std::string &clientid, const std::string &username, const std::string &password,
                          const std::vector<std::pair<std::string, std::string>> *userProperties, const std::weak_ptr<Client> &client, const bool allowAnonymous);
    AuthResult extendedAuth(const std::string &clientid, ExtendedAuthStage stage, const std::string &authMethod,
                            const std::string &authData, const std::vector<std::pair<std::string, std::string>> *userProperties, std::string &returnData,
                            std::string &username, const std::weak_ptr<Client> &client);
    bool alterSubscribe(const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics, uint8_t &qos,
                        const std::vector<std::pair<std::string, std::string>> *userProperties);
    bool alterPublish(const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics, std::string_view payload,
                      uint8_t &qos, bool &retain, std::vector<std::pair<std::string, std::string>> *userProperties);
    void clientDisconnected(const std::string &clientid);
    void fdReady(int fd, int events, const std::weak_ptr<void> &p);

    void setQuitting();
    void loadMosquittoPasswordFile();
    void loadMosquittoAclFile();
    AuthResult aclCheckFromMosquittoAclFile(const std::string &clientid, const std::string &username, const std::vector<std::string> &subtopics, AclAccess access);
    std::optional<AuthResult> unPwdCheckFromMosquittoPasswordFile(const std::string &username, const std::string &password);

    void periodicEvent();

};

#endif // PLUGIN_H
