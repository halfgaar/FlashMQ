#include "mainappthread.h"

MainAppThread::MainAppThread(QObject *parent) : QThread(parent)
{
    MainApp::initMainApp(1, nullptr);
    appInstance = MainApp::getMainApp();
}

void MainAppThread::run()
{
    appInstance->start();
}

void MainAppThread::stopApp()
{
    appInstance->quit();
    wait();
}

void MainAppThread::waitForStarted()
{
    int n = 0;
    while(!appInstance->getStarted())
    {
        QThread::msleep(10);

        if (n++ > 500)
            throw new std::runtime_error("Waiting for app to start failed.");
    }
}
