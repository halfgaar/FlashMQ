/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef AUTHPLUGIN_H
#define AUTHPLUGIN_H

#include <string>
#include <cstring>

#include "logger.h"
#include "configfileparser.h"

// Compatible with Mosquitto
enum class AclAccess
{
    none = 0,
    read = 1,
    write = 2
};

// Compatible with Mosquitto
enum class AuthResult
{
    success = 0,
    acl_denied = 12,
    login_denied = 11,
    error = 13
};

/**
 * @brief The MosquittoPasswordFileEntry struct stores the decoded base64 password salt and hash.
 *
 * The Mosquitto encrypted format looks like that of crypt(2), but it's not. This is an example entry:
 *
 * one:$6$emTXKCHfxMnZLDWg$gDcJRPojvOX8l7W/DRhSPoxV3CgPfECJVGRzw2Sqjdc2KIQ/CVLS1mNEuZUsp/vLdj7RCuqXCkgG43+XIc8WBA==
 *
 * $ is the seperator. '6' is hard-coded by the 'mosquitto_passwd' utility.
 */
struct MosquittoPasswordFileEntry
{
    std::vector<char> salt;
    std::vector<char> cryptedPassword;

    MosquittoPasswordFileEntry(const std::vector<char> &&salt, const std::vector<char> &&cryptedPassword);

    // The plan was that objects of this type wouldn't be copied, but I can't get emplacing to work without it...?
    //MosquittoPasswordFileEntry(const MosquittoPasswordFileEntry &other) = delete;
};

typedef int (*F_auth_plugin_version)(void);

typedef int (*F_auth_plugin_init_v2)(void **, struct mosquitto_auth_opt *, int);
typedef int (*F_auth_plugin_cleanup_v2)(void *, struct mosquitto_auth_opt *, int);
typedef int (*F_auth_plugin_security_init_v2)(void *, struct mosquitto_auth_opt *, int, bool);
typedef int (*F_auth_plugin_security_cleanup_v2)(void *, struct mosquitto_auth_opt *, int, bool);
typedef int (*F_auth_plugin_acl_check_v2)(void *, const char *, const char *, const char *, int);
typedef int (*F_auth_plugin_unpwd_check_v2)(void *, const char *, const char *);
typedef int (*F_auth_plugin_psk_key_get_v2)(void *, const char *, const char *, char *, int);

extern "C"
{
    // Gets called by the plugin, so it needs to exist, globally
    void mosquitto_log_printf(int level, const char *fmt, ...);
}

std::string AuthResultToString(AuthResult r);

/**
 * @brief The Authentication class handles our integrated authentication, but also supports loading Mosquitto auth
 * plugin compatible .so files.
 */
class Authentication
{
    F_auth_plugin_version version = nullptr;
    F_auth_plugin_init_v2 init_v2 = nullptr;
    F_auth_plugin_cleanup_v2 cleanup_v2 = nullptr;
    F_auth_plugin_security_init_v2 security_init_v2 = nullptr;
    F_auth_plugin_security_cleanup_v2 security_cleanup_v2 = nullptr;
    F_auth_plugin_acl_check_v2 acl_check_v2 = nullptr;
    F_auth_plugin_unpwd_check_v2 unpwd_check_v2 = nullptr;
    F_auth_plugin_psk_key_get_v2 psk_key_get_v2 = nullptr;

    static std::mutex initMutex;
    static std::mutex authChecksMutex;

    Settings &settings; // A ref because I want it to always be the same as the thread's settings

    void *pluginData = nullptr;
    Logger *logger = nullptr;
    bool initialized = false;
    bool useExternalPlugin = false;
    bool quitting = false;

    /**
     * @brief mosquittoPasswordFile is a once set value based on config. It's not reloaded on reload signal currently, because it
     * forces some decisions when you change files or remove the config option. For instance, do you remove all accounts loaded
     * from the previous one? Perhaps I'm overthinking it.
     *
     * Its content is, however, reloaded every two seconds.
     */
    const std::string mosquittoPasswordFile;

    struct timespec mosquittoPasswordFileLastLoad;

    std::unique_ptr<std::unordered_map<std::string, MosquittoPasswordFileEntry>> mosquittoPasswordEntries;
    EVP_MD_CTX *mosquittoDigestContext = nullptr;
    const EVP_MD *sha512 = EVP_sha512();

    void *loadSymbol(void *handle, const char *symbol) const;
public:
    Authentication(Settings &settings);
    Authentication(const Authentication &other) = delete;
    Authentication(Authentication &&other) = delete;
    ~Authentication();

    void loadPlugin(const std::string &pathToSoFile);
    void init();
    void cleanup();
    void securityInit(bool reloading);
    void securityCleanup(bool reloading);
    AuthResult aclCheck(const std::string &clientid, const std::string &username, const std::string &topic, AclAccess access);
    AuthResult unPwdCheck(const std::string &username, const std::string &password);

    void setQuitting();
    void loadMosquittoPasswordFile();
    AuthResult unPwdCheckFromMosquittoPasswordFile(const std::string &username, const std::string &password);

};

#endif // AUTHPLUGIN_H
