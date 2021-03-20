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


class AuthPlugin
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
    bool wanted = false;
    bool quitting = false;

    void *loadSymbol(void *handle, const char *symbol) const;
public:
    AuthPlugin(Settings &settings);
    AuthPlugin(const AuthPlugin &other) = delete;
    AuthPlugin(AuthPlugin &&other) = delete;
    ~AuthPlugin();

    void loadPlugin(const std::string &pathToSoFile);
    void init();
    void cleanup();
    void securityInit(bool reloading);
    void securityCleanup(bool reloading);
    AuthResult aclCheck(const std::string &clientid, const std::string &username, const std::string &topic, AclAccess access);
    AuthResult unPwdCheck(const std::string &username, const std::string &password);

    void setQuitting();

};

#endif // AUTHPLUGIN_H
