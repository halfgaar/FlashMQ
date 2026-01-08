#ifndef TESTPLUGIN_H
#define TESTPLUGIN_H

#include <memory>
#include <vector>
#include <thread>
#include "../../forward_declarations.h"

#include <curl/curl.h>

struct AuthenticatingClient
{
    std::weak_ptr<Client> client;
    std::vector<char> response;
    std::unique_ptr<CURL, void(*)(CURL*)> easy_handle;
    std::weak_ptr<CURLM> registeredAtMultiHandle;

public:
    AuthenticatingClient();
    ~AuthenticatingClient();

    void addToMulti(std::shared_ptr<CURLM> &curlMulti);
};


class TestPluginData
{
public:
    std::thread t;
    std::weak_ptr<Client> c;
    bool main_init_ran = false;
    std::shared_ptr<CURLM> curlMulti;
    uint32_t current_timer = 0;

    // Normally we keep some kind of indexed record of requests, but in our test plugin, we just track one.
    std::unique_ptr<AuthenticatingClient> curlTestClient;

public:
    TestPluginData();
    ~TestPluginData();
};



#endif // TESTPLUGIN_H
