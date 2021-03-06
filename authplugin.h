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

    Settings &settings; // A ref because I want it to always be the same as the thread's settings

    void *pluginData = nullptr;
    Logger *logger = nullptr;
    bool initialized = false;
    bool wanted = false;

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

};

#endif // AUTHPLUGIN_H
