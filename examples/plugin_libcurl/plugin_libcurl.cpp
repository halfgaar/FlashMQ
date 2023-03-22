/*
This file is part of FlashMQ example plugin 'plugin_libcurl'
and is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#include "vendor/flashmq_plugin.h"

#include <curl/curl.h>
#include <sys/epoll.h>
#include <stdexcept>

#include "pluginstate.h"
#include "curl_functions.h"
#include "authenticatingclient.h"


int flashmq_plugin_version()
{
    return FLASHMQ_PLUGIN_VERSION;
}

void flashmq_plugin_main_init(std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    if (curl_global_init(CURL_GLOBAL_ALL) != 0)
        throw std::runtime_error("Global curl init failed to init");
}

void flashmq_plugin_main_deinit(std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    curl_global_cleanup();
}

void flashmq_plugin_allocate_thread_memory(void **thread_data, std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    PluginState *state = new PluginState();
    *thread_data = state;
}

void flashmq_plugin_deallocate_thread_memory(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts)
{
    (void)plugin_opts;

    PluginState *state = static_cast<PluginState*>(thread_data);
    delete state;
}

/**
 * @brief flashmq_plugin_init We have nothing to do here, really.
 * @param thread_data
 * @param plugin_opts
 * @param reloading
 */
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

/**
 * @brief flashmq_plugin_poll_event_received
 * @param thread_data
 * @param fd
 * @param events
 * @param p A pointer to a data structure we assigned when watching the fd. We only use libcurl so we know we have to give it to
 *        libcurl. Had we also used something else, we would have needed this to figure out what the fd is.
 */
void flashmq_plugin_poll_event_received(void *thread_data, int fd, uint32_t events, const std::weak_ptr<void> &p)
{
    (void)p;

    PluginState *s = static_cast<PluginState*>(thread_data);

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
    curl_multi_socket_action(s->curlMulti, fd, new_events, &n);

    check_all_active_curls(s->curlMulti);
}


AuthResult flashmq_plugin_login_check(void *thread_data, const std::string &clientid, const std::string &username, const std::string &password,
                                      const std::vector<std::pair<std::string, std::string>> *userProperties, const std::weak_ptr<Client> &client)
{
    (void)clientid;
    (void)userProperties;
    (void)client;
    (void)username;
    (void)password;

    if (username == "deny")
    {
        return AuthResult::login_denied;
    }

    if (username == "curl")
    {
        PluginState *state = static_cast<PluginState*>(thread_data);

        // Libcurl is C, so we unfortunately have to use naked new and hope we'll delete it in all the right places.
        AuthenticatingClient *c = new AuthenticatingClient;
        c->client = client;
        c->globalData = state;

        curl_easy_setopt(c->eh, CURLOPT_WRITEFUNCTION, curl_write_cb); // The function that is called when curl has data from the response for us.
        curl_easy_setopt(c->eh, CURLOPT_WRITEDATA, c); // The pointer set we get in the above function.
        curl_easy_setopt(c->eh, CURLOPT_PRIVATE, c); // The pointer set we get back with 'curl_easy_getinfo', for when the request is finished.

        flashmq_logf(LOG_INFO, "Asking an HTTP server");

        // Keep in mind that DNS resovling may be blocking too. You could perhaps resolve the DNS once and use the result. But,
        // libcurl actually has some DNS caching as well.
        curl_easy_setopt(c->eh, CURLOPT_URL, "http://www.google.com/");

        if (!c->addToMulti(state->curlMulti))
        {
            delete c;
            return AuthResult::error;
        }

        return AuthResult::async;
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
    (void)(retain);
    (void)userProperties;
    (void)topic;

    return AuthResult::success;
}

