#include "../../flashmq_plugin.h"

int flashmq_auth_plugin_version()
{
    return FLASHMQ_PLUGIN_VERSION;
}

void flashmq_auth_plugin_allocate_thread_memory(void **thread_data, std::unordered_map<std::string, std::string> &auth_opts)
{
    *thread_data = malloc(1024);
    (void)auth_opts;
}

void flashmq_auth_plugin_deallocate_thread_memory(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts)
{
    free(thread_data);
    (void)auth_opts;
}

void flashmq_auth_plugin_init(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts, bool reloading)
{
    (void)thread_data;
    (void)auth_opts;
    (void)reloading;
}

void flashmq_auth_plugin_deinit(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts, bool reloading)
{
    (void)thread_data;
    (void)auth_opts;
    (void)reloading;

}

void flashmq_auth_plugin_periodic_event(void *thread_data)
{
    (void)thread_data;
}

AuthResult flashmq_auth_plugin_login_check(void *thread_data, const std::string &username, const std::string &password,
                                           const std::vector<std::pair<std::string, std::string>> *userProperties)
{
    (void)thread_data;
    (void)username;
    (void)password;
    (void)userProperties;

    return AuthResult::success;
}

AuthResult flashmq_auth_plugin_acl_check(void *thread_data, AclAccess access, const std::string &clientid, const std::string &username, const FlashMQMessage &msg)
{
    (void)thread_data;
    (void)access;
    (void)clientid;
    (void)username;
    (void)msg;

    return AuthResult::success;
}

AuthResult flashmq_extended_auth(void *thread_data, const std::string &clientid, ExtendedAuthStage stage, const std::string &authMethod,
                                 const std::string &authData, const std::vector<std::pair<std::string, std::string>> *userProperties, std::string &returnData,
                                 std::string &username)
{
    (void)thread_data;
    (void)stage;
    (void)authMethod;
    (void)authData;
    (void)username;
    (void)clientid;
    (void)userProperties;
    (void)returnData;

    return AuthResult::success;
}

