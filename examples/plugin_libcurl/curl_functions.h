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

#ifndef CURL_FUNCTIONS_H
#define CURL_FUNCTIONS_H

#include <curl/curl.h>
#include <sys/epoll.h>
#include "pluginstate.h"
#include "authenticatingclient.h"

int socket_event_watch_notification(CURL *easy, curl_socket_t s, int what,  void *clientp, void *socketp);
int timer_callback(CURLM *multi, long timeout_ms, void *clientp);
size_t curl_write_cb(char *data, size_t n, size_t l, void *userp);
void call_timed_curl_multi_socket_action(CURLM *multi, PluginState *s);
void check_all_active_curls(CURLM *curlMulti);


#endif // CURL_FUNCTIONS_H
