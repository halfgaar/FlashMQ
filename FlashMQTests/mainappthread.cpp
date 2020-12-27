#include "mainappthread.h"

MainAppThread::MainAppThread(QObject *parent) : QThread(parent)
{
    appInstance = MainApp::getMainApp();
}

void MainAppThread::run()
{
    appInstance->start();
}

void MainAppThread::stopApp()
{
    appInstance->quit();
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
