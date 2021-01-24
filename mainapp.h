#ifndef MAINAPP_H
#define MAINAPP_H

#include <iostream>
#include <sys/socket.h>
#include <stdexcept>
#include <netinet/in.h>
#include <fcntl.h>
#include <thread>
#include <vector>
#include <functional>
#include <forward_list>

#include "forward_declarations.h"

#include "utils.h"
#include "threaddata.h"
#include "client.h"
#include "mqttpacket.h"
#include "subscriptionstore.h"
#include "configfileparser.h"

class MainApp
{
    static MainApp *instance;

    bool started = false;
    bool running = true;
    std::vector<std::shared_ptr<ThreadData>> threads;
    std::shared_ptr<SubscriptionStore> subscriptionStore;
    std::unique_ptr<ConfigFileParser> confFileParser;
    std::forward_list<std::function<void()>> taskQueue;
    int epollFdAccept = -1;
    int taskEventFd = -1;
    std::mutex eventMutex;

    uint listenPort = 0;
    uint sslListenPort = 0;
    SSL_CTX *sslctx = nullptr;

    Logger *logger = Logger::getInstance();

    void loadConfig();
    void reloadConfig();
    static void doHelp(const char *arg);
    static void showLicense();
    void setCertAndKeyFromConfig();
    int createListenSocket(int portNr, bool ssl);

    MainApp(const std::string &configFilePath);
public:
    MainApp(const MainApp &rhs) = delete;
    MainApp(MainApp &&rhs) = delete;
    ~MainApp();
    static MainApp *getMainApp();
    static void initMainApp(int argc, char *argv[]);
    void start();
    void quit();
    bool getStarted() const {return started;}
    static void testConfig();


    void queueConfigReload();
};

#endif // MAINAPP_H
