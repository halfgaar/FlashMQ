#ifndef MAINAPPINTHREAD_H
#define MAINAPPINTHREAD_H

#include <thread>

#include "mainapp.h"
#include "mutexowned.h"

class MainAppInThread
{
    std::thread thread;
    const std::vector<std::string> mArgs;
    MutexOwned<std::shared_ptr<MainApp>> mMainApp;
public:
    MainAppInThread();
    MainAppInThread(const std::vector<std::string> &args);
    ~MainAppInThread();

    void start();
    void stopApp();
    void waitForStarted();
};

#endif // MAINAPPINTHREAD_H
