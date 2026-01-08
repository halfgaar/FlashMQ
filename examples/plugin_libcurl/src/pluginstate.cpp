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

#include "pluginstate.h"

#include <stdexcept>

#include "curl_functions.h"

ExampleCurlPlugin::PluginState::PluginState() :
    curlMulti(curl_multi_init(), curl_multi_cleanup)
{
    if (!curlMulti)
        throw std::runtime_error("Curl failed to init");

    curl_multi_setopt(curlMulti.get(), CURLMOPT_SOCKETFUNCTION, socket_event_watch_notification);
    curl_multi_setopt(curlMulti.get(), CURLMOPT_TIMERFUNCTION, timer_callback);
    curl_multi_setopt(curlMulti.get(), CURLMOPT_TIMERDATA, this); // We need our plugin state in the timer_callback function.
}

ExampleCurlPlugin::PluginState::~PluginState()
{

}

void ExampleCurlPlugin::PluginState::processNetworkAuthResult(std::weak_ptr<Client> &client, const std::string &answer)
{
    auto pos = this->networkAuthRequests.find(client);

    if (pos == this->networkAuthRequests.end())
        return;

    // This just checks we get an HTML page back, but you will of course need to do something more useful, like parse JSON,
    // look at the HTTP status code, etc.
    if (answer == "<!doctype")
        flashmq_continue_async_authentication_v4(client, AuthResult::success, std::string(), std::string(), 0);
    else
        flashmq_continue_async_authentication_v4(client, AuthResult::login_denied, std::string(), std::string(), 1000);

    this->networkAuthRequests.erase(pos);
}

void ExampleCurlPlugin::PluginState::clearAllNetworkRequests()
{
    this->networkAuthRequests.clear();
}
