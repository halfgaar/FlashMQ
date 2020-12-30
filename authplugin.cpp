#include "authplugin.h"

#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <dlfcn.h>

#include "exceptions.h"

// TODO: error handling on all the calls to the plugin. Exceptions? Passing to the caller?
// TODO: where to do the conditionals about whether the plugin is loaded, what to do on error, etc?
//       -> Perhaps merely log the error (and return 'denied'?)?

void mosquitto_log_printf(int level, const char *fmt, ...)
{
    Logger *logger = Logger::getInstance();
    va_list valist;
    va_start(valist, fmt);
    logger->logf(level, fmt);
    va_end(valist);
}


AuthPlugin::AuthPlugin() // our configuration object as param
{
    logger = Logger::getInstance();
}

void *AuthPlugin::loadSymbol(void *handle, const char *symbol)
{
    void *r = dlsym(handle, symbol);

    if (r == NULL)
    {
        std::string errmsg(dlerror());
        throw FatalError(errmsg);
    }

    return r;
}

void AuthPlugin::loadPlugin(const std::string &pathToSoFile)
{
    logger->logf(LOG_INFO, "Loading auth plugin %s", pathToSoFile.c_str());

    if (access(pathToSoFile.c_str(), R_OK) != 0)
    {
        std::ostringstream oss;
        oss << "Error loading auth plugin: The file " << pathToSoFile << " is not there or not readable";
        throw FatalError(oss.str());
    }

    void *r = dlopen(pathToSoFile.c_str(), RTLD_NOW|RTLD_GLOBAL);

    if (r == NULL)
    {
        std::string errmsg(dlerror());
        throw FatalError(errmsg);
    }

    version = (F_auth_plugin_version)loadSymbol(r, "mosquitto_auth_plugin_version");

    if (version() != 2)
    {
        throw FatalError("Only Mosquitto plugin version 2 is supported at this time.");
    }

    init_v2 = (F_auth_plugin_init_v2)loadSymbol(r, "mosquitto_auth_plugin_init");
    cleanup_v2 = (F_auth_plugin_cleanup_v2)loadSymbol(r, "mosquitto_auth_plugin_cleanup");
    security_init_v2 = (F_auth_plugin_security_init_v2)loadSymbol(r, "mosquitto_auth_security_init");
    security_cleanup_v2 = (F_auth_plugin_security_cleanup_v2)loadSymbol(r, "mosquitto_auth_security_cleanup");
    acl_check_v2 = (F_auth_plugin_acl_check_v2)loadSymbol(r, "mosquitto_auth_acl_check");
    unpwd_check_v2 = (F_auth_plugin_unpwd_check_v2)loadSymbol(r, "mosquitto_auth_unpwd_check");
    psk_key_get_v2 = (F_auth_plugin_psk_key_get_v2)loadSymbol(r, "mosquitto_auth_psk_key_get");
}

int AuthPlugin::init()
{
    struct mosquitto_auth_opt auth_opts[2]; // TODO: get auth opts from central config object
    std::memset(&auth_opts, 0, sizeof(struct mosquitto_auth_opt) * 2);
    int result = init_v2(&pluginData, auth_opts, 2);
    return result;
}

int AuthPlugin::cleanup()
{
    struct mosquitto_auth_opt auth_opts[2]; // TODO: get auth opts from central config object
    std::memset(&auth_opts, 0, sizeof(struct mosquitto_auth_opt) * 2);
    return cleanup_v2(pluginData, auth_opts, 2);
}

int AuthPlugin::securityInit(bool reloading)
{
    struct mosquitto_auth_opt auth_opts[2]; // TODO: get auth opts from central config object
    std::memset(&auth_opts, 0, sizeof(struct mosquitto_auth_opt) * 2);
    return security_init_v2(pluginData, auth_opts, 2, reloading);
}

int AuthPlugin::securityCleanup(bool reloading)
{
    struct mosquitto_auth_opt auth_opts[2]; // TODO: get auth opts from central config object
    std::memset(&auth_opts, 0, sizeof(struct mosquitto_auth_opt) * 2);
    return security_cleanup_v2(pluginData, auth_opts, 2, reloading);
}

AuthResult AuthPlugin::aclCheck(const std::string &clientid, const std::string &username, const std::string &topic, AclAccess access)
{
    int result = acl_check_v2(pluginData, clientid.c_str(), username.c_str(), topic.c_str(), static_cast<int>(access));
    return static_cast<AuthResult>(result);
}

AuthResult AuthPlugin::unPwdCheck(const std::string &username, const std::string &password)
{
    int result = unpwd_check_v2(pluginData, username.c_str(), password.c_str());
    return static_cast<AuthResult>(result);
}


