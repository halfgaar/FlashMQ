# Async auth using libcurl

This example plugin demonstrates the use of the async authentication interface in the `flashmq_plugin.h` interface, using libcurl. An HTTP request can take a while and in the mean time, we need to keep FlashMQ moving. The async interface allows us to do that.

A breakdown:

* The file `plugin_libcurl.cpp` contains the implementations of the plugin functions.
* Libcurl is told using `CURLMOPT_SOCKETFUNCTION` what function we'll use to call `flashmq_poll_add_fd()`.
* Libcurl is told using `CURLMOPT_TIMERFUNCTION` what function we'll use to call `flashmq_add_task()` and `flashmq_remove_task()`.
* On login, it initiates an HTTP request and returns `AuthResult::async`. FlashMQ will keep the client waiting while handling other requests.
* Socket activity is reported by `flashmq_plugin_poll_event_received()`, at which time we tell curl to continue with that socket.
* In `check_all_active_curls()`, when we detect the transfer has finished and we make a decision about the authentication, by submitting `AuthResult::success` or `AuthResult::login_denied` to `flashmq_continue_async_authentication()`.

## Building

Calling `build.sh` should be enough. It requires libcurl to be installed, of course.

## Configuring

Use the config option `plugin` to point to the so file, like:

```
allow_anonymous false
plugin /home/me/stuff/libplugin_libcurl.so.1.0.0
```

## Running

One you have FlashMQ running with the plugin, you can log in with username `deny` to test denying login works. If you log in with username `curl`, it will do a request to Google. If it sees HTML in the response, it will pass the authentication.
