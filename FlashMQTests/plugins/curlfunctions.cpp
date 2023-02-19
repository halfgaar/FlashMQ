#include "curlfunctions.h"
#include <sys/epoll.h>
#include "../../flashmq_plugin.h"
#include "test_plugin.h"
#include <cstring>

/**
 * @brief This is curl telling us what events to watch for.
 * @param easy
 * @param s
 * @param what
 * @param clientp
 * @param socketp
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

        flashmq_poll_add_fd(s, events, std::weak_ptr<void>());
    }

    return 0;
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

            if (answer == "<!doctype")
                flashmq_continue_async_authentication(c->client, AuthResult::success, std::string(), std::string());
            else
                flashmq_continue_async_authentication(c->client, AuthResult::login_denied, std::string(), std::string());

            delete c;
        }
    }
}

void call_timed_curl_multi_socket_action(CURLM *multi, TestPluginData *p)
{
    p->current_timer = 0;

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

int timer_callback(CURLM *multi, long timeout_ms, void *clientp)
{
    TestPluginData *p = static_cast<TestPluginData*>(clientp);

    // We also remove the last known task before it executes if curl tells us to install a new one. This
    // is suggested by the unclear and incomplete example at https://curl.se/libcurl/c/CURLMOPT_TIMERFUNCTION.html.
    if (timeout_ms == -1 || p->current_timer > 0)
    {
        flashmq_remove_task(p->current_timer);
        p->current_timer = 0;
    }

    if (timeout_ms >= 0)
    {
        auto f = std::bind(&call_timed_curl_multi_socket_action, multi, p);
        p->current_timer = flashmq_add_task(f, timeout_ms);
    }
    return CURLM_OK;
}

size_t curl_write_cb(char *data, size_t n, size_t l, void *userp)
{
    AuthenticatingClient *ac = static_cast<AuthenticatingClient*>(userp);

    int pos = ac->response.size();
    ac->response.resize(ac->response.size() + n*l);
    std::memcpy(&ac->response[pos], data, n*l);

    return n*l;
}
