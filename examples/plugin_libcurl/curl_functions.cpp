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

#include "curl_functions.h"

#include <cstring>

#include "vendor/flashmq_plugin.h"
#include "pluginstate.h"
#include "authenticatingclient.h"

/**
 * @brief This is curl telling us what events to watch for.
 * @param easy Is an 'easy handle'. You make one per request.
 * @param s The socket of the HTTP request.
 * @param what What events to listen for
 * @param clientp Pointer set with CURLMOPT_SOCKETDATA. It can be whatever you need.
 * @param socketp Is set with curl_multi_assign or will be NULL.
 * @return
 */
int socket_event_watch_notification(CURL *easy, curl_socket_t s, int what,  void *clientp, void *socketp)
{
    (void)easy;
    (void)clientp;
    (void)socketp;

    if (what == CURL_POLL_REMOVE)
        flashmq_poll_remove_fd(s);
    else
    {
        int events = 0;

        if (what == CURL_POLL_IN)
            events |= EPOLLIN;
        else if (what == CURL_POLL_OUT)
            events |= EPOLLOUT;
        else if (what == CURL_POLL_INOUT)
            events = EPOLLIN | EPOLLOUT;
        else
            return 1;

        // We know we get back a socket for curl, but if there are multiple libs we use, we could have used the weak void pointer to associate
        // a data structure with the socket, which you get back on socket events.
        flashmq_poll_add_fd(s, events, std::weak_ptr<void>());
    }

    return 0;
}

int timer_callback(CURLM *multi, long timeout_ms, void *clientp)
{
    PluginState *s = static_cast<PluginState*>(clientp);

    // We also remove the last known task before it executes if curl tells us to install a new one. This
    // is suggested by the unclear and incomplete example at https://curl.se/libcurl/c/CURLMOPT_TIMERFUNCTION.html.
    if (timeout_ms == -1 || s->current_timer > 0)
    {
        flashmq_remove_task(s->current_timer);
        s->current_timer = 0;
    }

    if (timeout_ms >= 0)
    {
        auto f = std::bind(&call_timed_curl_multi_socket_action, multi, s);
        s->current_timer = flashmq_add_task(f, timeout_ms);
    }
    return CURLM_OK;
}

void call_timed_curl_multi_socket_action(CURLM *multi, PluginState *s)
{
    s->current_timer = 0;

    int a = 0;
    int rc = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &a);

    /* Curl says: "When this function returns error, the state of all transfers are uncertain and they cannot be
     * continued. curl_multi_socket_action should not be called again on the same multi handle after an error has
     * been returned, unless first removing all the handles and adding new ones."
     *
     * It's not clear to me how to remove them all. Is this right? Or will this not give in-progress ones? The
     * API doesn't seem to have a function to get all handles. Do I have to do external book-keeping?
     */
    if (rc != CURLM_OK)
    {
        CURLMsg *msg;
        int msgs_left;
        while((msg = curl_multi_info_read(multi, &msgs_left)))
        {
            if (msg->msg == CURLMSG_DONE)
            {
                CURL *easy = msg->easy_handle;
                AuthenticatingClient *c = nullptr;
                curl_easy_getinfo(easy, CURLINFO_PRIVATE, &c);
                delete c;
            }
        }
    }

    check_all_active_curls(multi);
}

void check_all_active_curls(CURLM *curlMulti)
{
    CURLMsg *msg;
    int msgs_left;
    while((msg = curl_multi_info_read(curlMulti, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE)
        {
            CURL *easy = msg->easy_handle;
            AuthenticatingClient *c = nullptr;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &c);

            flashmq_logf(LOG_INFO, "Libcurl said: %s", curl_easy_strerror(msg->data.result));

            std::string answer(c->response.data(), std::min<int>(9, c->response.size()));

            // This just checks we get an HTML page back, but you will of course need to do something more useful, like parse JSON,
            // look at the HTTP status code, etc.
            if (answer == "<!doctype")
                flashmq_continue_async_authentication(c->client, AuthResult::success, std::string(), std::string());
            else
                flashmq_continue_async_authentication(c->client, AuthResult::login_denied, std::string(), std::string());

            delete c;
        }
    }
}

/**
 * @brief curl_write_cb Would be more accurately 'read callback', because it's used to read the response from the curl easy handle.
 * @param data
 * @param n
 * @param l
 * @param userp is whatever you set with CURLOPT_WRITEDATA.
 * @return
 */
size_t curl_write_cb(char *data, size_t n, size_t l, void *userp)
{
    AuthenticatingClient *ac = static_cast<AuthenticatingClient*>(userp);

    int pos = ac->response.size();
    ac->response.resize(ac->response.size() + n*l);
    std::memcpy(&ac->response[pos], data, n*l);

    return n*l;
}
