#include <functional>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <optional>

#include "../../flashmq_plugin.h"
#include "test_plugin.h"

#include <curl/curl.h>
#include <sys/epoll.h>
#include "curlfunctions.h"


TestPluginData::TestPluginData() :
    curlMulti(curl_multi_init(), curl_multi_cleanup)
{
    if (!curlMulti)
        throw std::runtime_error("Curl failed to init");

    curl_multi_setopt(curlMulti.get(), CURLMOPT_SOCKETFUNCTION, socket_event_watch_notification);
    curl_multi_setopt(curlMulti.get(), CURLMOPT_TIMERFUNCTION, timer_callback);
    curl_multi_setopt(curlMulti.get(), CURLMOPT_TIMERDATA, this);
}

TestPluginData::~TestPluginData()
{
    if (this->t.joinable())
        t.join();
}

void get_auth_result_delayed(std::weak_ptr<Client> client, AuthResult result)
{
    usleep(500000);

    flashmq_continue_async_authentication_v4(client, result, "", "", 0);
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
}

void flashmq_plugin_deallocate_thread_memory(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    TestPluginData *p = static_cast<TestPluginData*>(thread_data);
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
    if (curl_multi_socket_action(p->curlMulti.get(), fd, new_events, &n) != CURLM_OK)
    {
        p->curlTestClient.reset();
        return;
    }

    check_all_active_curls(p, p->curlMulti.get());
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

    if (username.find("async") == 0)
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
        struct sockaddr_storage addr_mem;
        struct sockaddr *addr = reinterpret_cast<sockaddr*>(&addr_mem);
        socklen_t addrlen = sizeof(addr_mem);

        std::string text;
        flashmq_get_client_address_v4(client, &text, addr, &addrlen);

        sockaddr sockaddr_after;
        std::memcpy(&sockaddr_after, addr, std::min<socklen_t>(addrlen, sizeof(sockaddr)));

        flashmq_publish_message("getaddresstest/address", 0, false, text);

        if (sockaddr_after.sa_family == AF_INET)
        {
            flashmq_publish_message("getaddresstest/family", 0, false, "AF_INET");
        }
    }

    if (username == "curl")
    {
        TestPluginData *p = static_cast<TestPluginData*>(thread_data);

        p->curlTestClient = std::make_unique<AuthenticatingClient>();
        p->curlTestClient->client = client;

        curl_easy_setopt(p->curlTestClient->easy_handle.get(), CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(p->curlTestClient->easy_handle.get(), CURLOPT_WRITEDATA, p->curlTestClient.get());
        curl_easy_setopt(p->curlTestClient->easy_handle.get(), CURLOPT_PRIVATE, p->curlTestClient.get());

        // Keep in mind that DNS resovling may be blocking too. You could perhaps resolve the DNS once and use the result.
        curl_easy_setopt(p->curlTestClient->easy_handle.get(), CURLOPT_URL, "http://www.google.com/");

        p->curlTestClient->addToMulti(p->curlMulti);

        return AuthResult::async;
    }

    return AuthResult::success;
}

void publish_in_thread()
{
    flashmq_publish_message("topic/from/thread", 0, false, "payload from thread");
}

AuthResult flashmq_plugin_acl_check(void *thread_data, const AclAccess access, const std::string &clientid, const std::string &username,
                                    const std::string &topic, const std::vector<std::string> &subtopics, const std::string &shareName,
                                    std::string_view payload, const uint8_t qos, const bool retain,
                                    const std::optional<std::string> &correlationData, const std::optional<std::string> &responseTopic,
                                    const std::vector<std::pair<std::string, std::string>> *userProperties)
{
    (void)thread_data;
    (void)access;
    (void)clientid;
    (void)username;
    (void)subtopics;
    (void)qos;
    (void)retain;
    (void)correlationData;
    (void)responseTopic;
    (void)userProperties;
    (void)shareName;

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
    {
        std::weak_ptr<Session> ses;
        flashmq_get_session_pointer(clientid, username, ses);
        flashmq_plugin_remove_client_v4(ses, topic == "removeclientandsession", ServerDisconnectReasons::NormalDisconnect);
    }

    if (clientid == "unsubscribe" && access == AclAccess::write)
    {
        std::weak_ptr<Session> session;
        flashmq_get_session_pointer(clientid, username, session);
        flashmq_plugin_remove_subscription_v4(session, topic);
    }

    if (clientid == "generate_publish")
    {
        flashmq_logf(LOG_INFO, "Publishing from plugin.");

        const std::string topic = "generated/topic";
        const std::string payload = "money";
        flashmq_publish_message(topic, 0, false, payload);
    }

    if ((access == AclAccess::read || access == AclAccess::write) && topic == "test_user_property")
    {
        assert(userProperties);
    }

    if (topic == "publish_in_thread" && access == AclAccess::write)
    {
        std::thread t(publish_in_thread);
        pthread_setname_np(t.native_handle(), "PubInThread");
        t.join();
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
                                  uint8_t &qos, bool &retain, const std::optional<std::string> &correlationData, const std::optional<std::string> &responseTopic,
                                  std::vector<std::pair<std::string, std::string>> *userProperties)
{
    (void)thread_data;
    (void)clientid;
    (void)subtopics;
    (void)qos;
    (void)retain;
    (void)correlationData;
    (void)responseTopic;
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

AuthenticatingClient::AuthenticatingClient() :
    easy_handle(curl_easy_init(), curl_easy_cleanup)
{

}

AuthenticatingClient::~AuthenticatingClient()
{
    auto x = registeredAtMultiHandle.lock();

    if (x)
    {
        curl_multi_remove_handle(x.get(), easy_handle.get());
    }
}

void AuthenticatingClient::addToMulti(std::shared_ptr<CURLM> &curlMulti)
{
    if (curl_multi_add_handle(curlMulti.get(), easy_handle.get()) != CURLM_OK)
        throw std::runtime_error("curl_multi_add_handle failed");

    registeredAtMultiHandle = curlMulti;
}
