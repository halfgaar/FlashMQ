#ifndef TESTPLUGIN_H
#define TESTPLUGIN_H

#include <unordered_map>
#include <vector>
#include <thread>
#include "../../forward_declarations.h"

#include <curl/curl.h>

class TestPluginData
{
public:
    std::thread t;

    std::weak_ptr<Client> c;

    bool main_init_ran = false;

    CURLM *curlMulti;

    uint32_t current_timer = 0;

public:
    ~TestPluginData();
};

struct AuthenticatingClient
{
    TestPluginData *globalData;
    std::weak_ptr<Client> client;
    std::vector<char> response;
    CURL *eh = nullptr;
    CURLM *curlMulti = nullptr;

    void cleanup();

public:
    AuthenticatingClient();
    ~AuthenticatingClient();

    bool addToMulti(CURLM *curlMulti);
};

#endif // TESTPLUGIN_H
