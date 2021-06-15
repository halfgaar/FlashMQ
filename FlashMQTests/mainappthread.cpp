/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#include "mainappthread.h"

MainAppThread::MainAppThread(QObject *parent) : QThread(parent)
{
    if (appInstance)
    {
        delete appInstance;
    }
    appInstance = nullptr;
    MainApp::instance = nullptr;
    MainApp::initMainApp(1, nullptr);
    appInstance = MainApp::getMainApp();
    appInstance->settings->allowAnonymous = true;
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
