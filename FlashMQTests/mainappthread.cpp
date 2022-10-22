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
    MainApp::initMainApp(1, nullptr);
    appInstance = MainApp::getMainApp();
    appInstance->settings.allowAnonymous = true;
}

MainAppThread::MainAppThread(const std::vector<std::string> &args, QObject *parent) : QThread(parent)
{
    std::list<std::vector<char>> argCopies;

    const std::string programName = "FlashMQTests";
    std::vector<char> programNameCopy(programName.size() + 1, 0);
    std::copy(programName.begin(), programName.end(), programNameCopy.begin());
    argCopies.push_back(std::move(programNameCopy));

    for (const std::string &arg : args)
    {
        std::vector<char> copyArg(arg.size() + 1, 0);
        std::copy(arg.begin(), arg.end(), copyArg.begin());
        argCopies.push_back(std::move(copyArg));
    }

    char *argv[256];
    memset(argv, 0, 256*sizeof (char*));

    int i = 0;
    for (std::vector<char> &copy : argCopies)
    {
        argv[i++] = copy.data();
    }

    MainApp::initMainApp(i, argv);
    appInstance = MainApp::getMainApp();

    // A hack: when I supply args I probably define a config for auth stuff.
    if (args.empty())
        appInstance->settings.allowAnonymous = true;
}

MainAppThread::~MainAppThread()
{
    if (appInstance)
    {
        delete appInstance;
        MainApp::instance = nullptr;
    }
    appInstance = nullptr;
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
            throw std::runtime_error("Waiting for app to start failed.");
    }
}
