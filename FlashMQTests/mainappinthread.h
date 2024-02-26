#ifndef MAINAPPINTHREAD_H
#define MAINAPPINTHREAD_H

#include <thread>

#include "mainapp.h"

class MainAppInThread
{
    std::thread thread;
    MainApp *appInstance = nullptr;
public:
    MainAppInThread();
    MainAppInThread(const std::vector<std::string> &args);
    ~MainAppInThread();

    void start();
    void stopApp();
    void waitForStarted();
    std::shared_ptr<SubscriptionStore> getStore();
};

#endif // MAINAPPINTHREAD_H
