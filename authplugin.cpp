#include "authplugin.h"

#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <dlfcn.h>

#include "exceptions.h"
#include "unscopedlock.h"

std::mutex AuthPlugin::initMutex;
std::mutex AuthPlugin::authChecksMutex;

void mosquitto_log_printf(int level, const char *fmt, ...)
{
    Logger *logger = Logger::getInstance();
    va_list valist;
    va_start(valist, fmt);
    logger->logf(level, fmt, valist);
    va_end(valist);
}


AuthPlugin::AuthPlugin(Settings &settings) :
    settings(settings)
{
    logger = Logger::getInstance();
}

AuthPlugin::~AuthPlugin()
{
    cleanup();
}

void *AuthPlugin::loadSymbol(void *handle, const char *symbol) const
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
    if (pathToSoFile.empty())
        return;

    logger->logf(LOG_NOTICE, "Loading auth plugin %s", pathToSoFile.c_str());

    initialized = false;
    wanted = true;

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

    initialized = true;
}

void AuthPlugin::init()
{
    if (!wanted)
        return;

    UnscopedLock lock(initMutex);
    if (settings.authPluginSerializeInit)
        lock.lock();

    if (quitting)
        return;

    AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
    int result = init_v2(&pluginData, authOpts.head(), authOpts.size());
    if (result != 0)
        throw FatalError("Error initialising auth plugin.");
}

void AuthPlugin::cleanup()
{
    if (!cleanup_v2)
        return;

    securityCleanup(false);

    AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
    int result = cleanup_v2(pluginData, authOpts.head(), authOpts.size());
    if (result != 0)
        logger->logf(LOG_ERR, "Error cleaning up auth plugin"); // Not doing exception, because we're shutting down anyway.
}

void AuthPlugin::securityInit(bool reloading)
{
    if (!wanted)
        return;

    UnscopedLock lock(initMutex);
    if (settings.authPluginSerializeInit)
        lock.lock();

    if (quitting)
        return;

    AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
    int result = security_init_v2(pluginData, authOpts.head(), authOpts.size(), reloading);
    if (result != 0)
    {
        throw AuthPluginException("Plugin function mosquitto_auth_security_init returned an error. If it didn't log anything, we don't know what it was.");
    }
    initialized = true;
}

void AuthPlugin::securityCleanup(bool reloading)
{
    if (!wanted)
        return;

    initialized = false;
    AuthOptCompatWrap &authOpts = settings.getAuthOptsCompat();
    int result = security_cleanup_v2(pluginData, authOpts.head(), authOpts.size(), reloading);

    if (result != 0)
    {
        throw AuthPluginException("Plugin function mosquitto_auth_security_cleanup returned an error. If it didn't log anything, we don't know what it was.");
    }
}

AuthResult AuthPlugin::aclCheck(const std::string &clientid, const std::string &username, const std::string &topic, AclAccess access)
{
    if (!wanted)
        return AuthResult::success;

    if (!initialized)
    {
        logger->logf(LOG_ERR, "ACL check wanted, but initialization failed.  Can't perform check.");
        return AuthResult::error;
    }

    UnscopedLock lock(authChecksMutex);
    if (settings.authPluginSerializeAuthChecks)
        lock.lock();

    int result = acl_check_v2(pluginData, clientid.c_str(), username.c_str(), topic.c_str(), static_cast<int>(access));
    AuthResult result_ = static_cast<AuthResult>(result);

    if (result_ == AuthResult::error)
    {
        logger->logf(LOG_ERR, "ACL check by plugin returned error for topic '%s'. If it didn't log anything, we don't know what it was.", topic.c_str());
    }

    return result_;
}

AuthResult AuthPlugin::unPwdCheck(const std::string &username, const std::string &password)
{
    if (!wanted)
        return AuthResult::success;

    if (!initialized)
    {
        logger->logf(LOG_ERR, "Username+password check wanted, but initialization failed. Can't perform check.");
        return AuthResult::error;
    }

    UnscopedLock lock(authChecksMutex);
    if (settings.authPluginSerializeAuthChecks)
        lock.lock();

    int result = unpwd_check_v2(pluginData, username.c_str(), password.c_str());
    AuthResult r = static_cast<AuthResult>(result);

    if (r == AuthResult::error)
    {
        logger->logf(LOG_ERR, "Username+password check by plugin returned error for user '%s'. If it didn't log anything, we don't know what it was.", username.c_str());
    }

    return r;
}

void AuthPlugin::setQuitting()
{
    this->quitting = true;
}

std::string AuthResultToString(AuthResult r)
{
    {
        if (r == AuthResult::success)
            return "success";
        if (r == AuthResult::acl_denied)
            return "ACL denied";
        if (r == AuthResult::login_denied)
            return "login Denied";
        if (r == AuthResult::error)
            return "error in check";
    }

    return "";
}
