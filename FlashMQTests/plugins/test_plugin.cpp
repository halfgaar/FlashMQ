#include <functional>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <stdexcept>

#include "../../flashmq_plugin.h"
#include "test_plugin.h"

#include <curl/curl.h>
#include <sys/epoll.h>
#include "curlfunctions.h"


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
    TestPluginData *p = new TestPluginData();
    *thread_data = p;
    (void)plugin_opts;

    p->curlMulti = curl_multi_init();

    if (p->curlMulti == nullptr)
        throw std::runtime_error("Curl failed to init");

    curl_multi_setopt(p->curlMulti, CURLMOPT_SOCKETFUNCTION, socket_event_watch_notification);
    curl_multi_setopt(p->curlMulti, CURLMOPT_TIMERFUNCTION, timer_callback);
    curl_multi_setopt(p->curlMulti, CURLMOPT_TIMERDATA, p);

}

void flashmq_plugin_deallocate_thread_memory(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    TestPluginData *p = static_cast<TestPluginData*>(thread_data);

    curl_multi_cleanup(p->curlMulti);

    delete p;
}

void flashmq_plugin_poll_event_received(void *thread_data, int fd, uint32_t events, const std::weak_ptr<void> &ptr)
{
    (void)ptr;

    TestPluginData *p = static_cast<TestPluginData*>(thread_data);

    int new_events = CURL_CSELECT_ERR;

    if (events & EPOLLIN)
    {
        new_events &= ~CURL_CSELECT_ERR;
        new_events |= CURL_CSELECT_IN;
    }
    if (events & EPOLLOUT)
    {
        new_events &= ~CURL_CSELECT_ERR;
        new_events |= CURL_CSELECT_OUT;
    }

    int n = -1;
    curl_multi_socket_action(p->curlMulti, fd, new_events, &n);

    check_all_active_curls(p->curlMulti);
}

void flashmq_plugin_init(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts, bool reloading)
{
    (void)thread_data;
    (void)plugin_opts;
    (void)reloading;

    TestPluginData *p = static_cast<TestPluginData*>(thread_data);

    if (plugin_opts.find("main_init was here") != plugin_opts.end())
        p->main_init_ran = true;
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

        if (p->t.joinable())
            p->t.join();

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

    if (username == "curl")
    {
        TestPluginData *p = static_cast<TestPluginData*>(thread_data);

        // Libcurl is C, so we unfortunately have to use naked new and hope we'll delete it in all the right places.
        AuthenticatingClient *c = new AuthenticatingClient;
        c->client = client;
        c->globalData = p;

        curl_easy_setopt(c->eh, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(c->eh, CURLOPT_WRITEDATA, c);
        curl_easy_setopt(c->eh, CURLOPT_PRIVATE, c);

        // Keep in mind that DNS resovling may be blocking too. You could perhaps resolve the DNS once and use the result.
        curl_easy_setopt(c->eh, CURLOPT_URL, "http://www.google.com/");

        // TODO: I don't like this error handling.
        if (!c->addToMulti(p->curlMulti))
            delete c;

        return AuthResult::async;
    }

    return AuthResult::success;
}

AuthResult flashmq_plugin_acl_check(void *thread_data, const AclAccess access, const std::string &clientid, const std::string &username,
                                    const std::string &topic, const std::vector<std::string> &subtopics, std::string_view payload,
                                    const uint8_t qos, const bool retain, const std::vector<std::pair<std::string, std::string>> *userProperties)
{
    (void)thread_data;
    (void)access;
    (void)clientid;
    (void)username;
    (void)subtopics;
    (void)qos;
    (void)retain;
    (void)userProperties;

    if (clientid == "return_error")
        return AuthResult::error;

    if (access == AclAccess::subscribe && clientid == "success_without_retained_delivery")
        return AuthResult::success_without_retained_delivery;

    if (clientid == "test_user_without_retain_as_published_CswU21YA" && access == AclAccess::read)
        assert(!retain);

    if (clientid == "test_user_with_retain_as_published_v8sIeCvI" && access == AclAccess::read)
        assert(retain);

    assert(access == AclAccess::subscribe || !payload.empty());

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

bool flashmq_plugin_alter_publish(void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics, std::string_view payload,
                                  uint8_t &qos, bool &retain, std::vector<std::pair<std::string, std::string>> *userProperties)
{
    (void)thread_data;
    (void)clientid;
    (void)subtopics;
    (void)qos;
    (void)retain;
    (void)userProperties;

    TestPluginData *p = static_cast<TestPluginData*>(thread_data);

    assert(!payload.empty());

    if (topic == "changeme")
    {
        topic = "changed";
        qos = 2;
        return true;
    }

    if (topic == "check_main_init_presence" && p->main_init_ran)
    {
        topic = "check_main_init_presence_confirmed";
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

void flashmq_plugin_main_init(std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    flashmq_logf(LOG_INFO, "The tester was here.");

    // The plugin_opts aren't const. I don't know if that was a mistake or not anymore, but it works in my favor now.
    plugin_opts["main_init was here"] = "true";

    if (curl_global_init(CURL_GLOBAL_ALL) != 0)
        throw std::runtime_error("Global curl init failed to init");
}

void flashmq_plugin_main_deinit(std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    curl_global_cleanup();
}

void AuthenticatingClient::cleanup()
{
    if (curlMulti)
    {
        curl_multi_remove_handle(curlMulti, eh);
        curlMulti = nullptr;
    }

    curl_easy_cleanup(eh);
    eh = nullptr;
}

AuthenticatingClient::AuthenticatingClient()
{
    eh = curl_easy_init();
}

AuthenticatingClient::~AuthenticatingClient()
{
    cleanup();
}

bool AuthenticatingClient::addToMulti(CURLM *curlMulti)
{
    if (curl_multi_add_handle(curlMulti, eh) != CURLM_OK)
    {
        cleanup();
        return false;
    }
    this->curlMulti = curlMulti;
    return true;
}
