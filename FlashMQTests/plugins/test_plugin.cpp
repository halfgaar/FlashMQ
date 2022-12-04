#include "functional"
#include <unistd.h>

#include "../../flashmq_plugin.h"
#include "test_plugin.h"


TestPluginData::~TestPluginData()
{
    if (this->t.joinable())
        t.join();
}

void get_auth_result_delayed(std::weak_ptr<Client> client, AuthResult result)
{
    usleep(500000);

    flashmq_continue_async_authentication(client, result, "", "");
}


int flashmq_plugin_version()
{
    return FLASHMQ_PLUGIN_VERSION;
}

void flashmq_plugin_allocate_thread_memory(void **thread_data, std::unordered_map<std::string, std::string> &plugin_opts)
{
    *thread_data = new TestPluginData();
    (void)plugin_opts;
}

void flashmq_plugin_deallocate_thread_memory(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts)
{
    TestPluginData *p = static_cast<TestPluginData*>(thread_data);
    delete p;
    (void)plugin_opts;
}

void flashmq_plugin_init(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts, bool reloading)
{
    (void)thread_data;
    (void)plugin_opts;
    (void)reloading;
}

void flashmq_plugin_deinit(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts, bool reloading)
{
    (void)thread_data;
    (void)plugin_opts;
    (void)reloading;

}

void flashmq_plugin_periodic_event(void *thread_data)
{
    (void)thread_data;
}

AuthResult flashmq_plugin_login_check(void *thread_data, const std::string &clientid, const std::string &username, const std::string &password,
                                      const std::vector<std::pair<std::string, std::string>> *userProperties, const std::weak_ptr<Client> &client)
{
    (void)thread_data;
    (void)clientid;
    (void)username;
    (void)password;
    (void)userProperties;
    (void)client;

    if (username == "async")
    {
        TestPluginData *p = static_cast<TestPluginData*>(thread_data);
        p->c = client;

        AuthResult result = password == "success" ? AuthResult::success : AuthResult::login_denied;

        auto delayedResult = std::bind(&get_auth_result_delayed, p->c, result);
        p->t = std::thread(delayedResult);

        return AuthResult::async;
    }

    if (username == "failme")
        return AuthResult::login_denied;

    if (username == "getaddress")
    {
        std::string text;
        FlashMQSockAddr addr;
        flashmq_get_client_address(client, &text, &addr);

        flashmq_publish_message("getaddresstest/address", 0, false, text);

        if (addr.getAddr()->sa_family == AF_INET)
        {
            flashmq_publish_message("getaddresstest/family", 0, false, "AF_INET");
        }
    }

    return AuthResult::success;
}

AuthResult flashmq_plugin_acl_check(void *thread_data, const AclAccess access, const std::string &clientid, const std::string &username,
                                    const std::string &topic, const std::vector<std::string> &subtopics, const uint8_t qos, const bool retain,
                                    const std::vector<std::pair<std::string, std::string>> *userProperties)
{
    (void)thread_data;
    (void)access;
    (void)clientid;
    (void)username;
    (void)subtopics;
    (void)qos;
    (void)retain;
    (void)userProperties;

    if (access == AclAccess::register_will && topic == "will/disallowed")
        return AuthResult::acl_denied;

    if (topic == "removeclient" || topic == "removeclientandsession")
        flashmq_plugin_remove_client(clientid, topic == "removeclientandsession", ServerDisconnectReasons::NormalDisconnect);

    if (clientid == "unsubscribe" && access == AclAccess::write)
        flashmq_plugin_remove_subscription(clientid, topic);

    if (clientid == "generate_publish")
    {
        flashmq_logf(LOG_INFO, "Publishing from plugin.");

        const std::string topic = "generated/topic";
        const std::string payload = "money";
        flashmq_publish_message(topic, 0, false, payload);
    }

    return AuthResult::success;
}

AuthResult flashmq_plugin_extended_auth(void *thread_data, const std::string &clientid, ExtendedAuthStage stage, const std::string &authMethod,
                                        const std::string &authData, const std::vector<std::pair<std::string, std::string>> *userProperties, std::string &returnData,
                                        std::string &username, const std::weak_ptr<Client> &client)
{
    (void)thread_data;
    (void)stage;
    (void)authMethod;
    (void)authData;
    (void)username;
    (void)clientid;
    (void)userProperties;
    (void)returnData;
    (void)client;

    if (authMethod == "always_good_passing_back_the_auth_data")
    {
        if (authData == "actually not good.")
            return AuthResult::login_denied;

        returnData = authData;
        return AuthResult::success;
    }
    if (authMethod == "always_fail")
    {
        return AuthResult::login_denied;
    }
    if (authMethod == "two_step")
    {
        if (authData == "Hello")
            returnData = "Hello back";

        if (authData == "grant me already!")
        {
            returnData = "OK, if you insist.";
            return AuthResult::success;
        }
        else if (authData == "whoops, wrong data.")
            return AuthResult::login_denied;
        else
            return AuthResult::auth_continue;
    }

    return AuthResult::auth_method_not_supported;
}

bool flashmq_plugin_alter_publish(void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
                                  uint8_t &qos, bool &retain, std::vector<std::pair<std::string, std::string>> *userProperties)
{
    (void)thread_data;
    (void)clientid;
    (void)subtopics;
    (void)qos;
    (void)retain;
    (void)userProperties;

    if (topic == "changeme")
    {
        topic = "changed";
        qos = 2;
        return true;
    }

    return false;
}

void flashmq_plugin_client_disconnected(void *thread_data, const std::string &clientid)
{
    (void)thread_data;

    flashmq_logf(LOG_INFO, "flashmq_plugin_client_disconnected called for '%s'", clientid.c_str());
    flashmq_publish_message("disconnect/confirmed", 0, false, "adsf");
}

