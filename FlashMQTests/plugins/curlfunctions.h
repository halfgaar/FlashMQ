#ifndef CURLFUNCTIONS_H
#define CURLFUNCTIONS_H

#include <curl/curl.h>
#include "test_plugin.h"

int socket_event_watch_notification(CURL *easy, curl_socket_t s, int what,  void *clientp, void *socketp);
void check_all_active_curls(TestPluginData *p, CURLM *curlMulti);
void call_timed_curl_multi_socket_action(CURLM *multi, TestPluginData *p);
int timer_callback(CURLM *multi, long timeout_ms, void *clientp);
size_t curl_write_cb(char *data, size_t n, size_t l, void *userp);

#endif // CURLFUNCTIONS_H
