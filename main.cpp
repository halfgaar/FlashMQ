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

#include <iostream>
#include <signal.h>
#include <memory>
#include <string.h>
#include <openssl/ssl.h>

#include "mainapp.h"

MainApp *mainApp = nullptr;

static void signal_handler(int signal)
{
    if (signal == SIGPIPE)
    {
        return;
    }
    if (signal == SIGHUP)
    {
        mainApp->queueConfigReload();
    }
    else if (signal == SIGTERM || signal == SIGINT)
    {
        mainApp->quit();
    }
    else
    {
        Logger *logger = Logger::getInstance();
        logger->logf(LOG_INFO, "Received unhandled signal %d", signal);
    }
}

int register_signal_handers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof (struct sigaction));
    sa.sa_handler = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGHUP, &sa, nullptr) != 0 || sigaction(SIGTERM, &sa, nullptr) != 0 || sigaction(SIGINT, &sa, nullptr) != 0)
    {
        Logger *logger = Logger::getInstance();
        logger->logf(LOG_ERR, "Error registering signal handlers");
        return -1;
    }

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set,SIGPIPE);

    int r;
    if ((r = sigprocmask(SIG_BLOCK, &set, NULL) != 0))
    {
        return r;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    try
    {
        MainApp::initMainApp(argc, argv);
        Logger *logger = Logger::getInstance();
        mainApp = MainApp::getMainApp();
        check<std::runtime_error>(register_signal_handers());
#ifdef NDEBUG
        logger->logf(LOG_NOTICE, "Starting FlashMQ version %s, release build.", VERSION);
#else
        logger->logf(LOG_NOTICE, "Starting FlashMQ version %s, debug build.", VERSION);
#endif
        mainApp->start();
    }
    catch (ConfigFileException &ex)
    {
        // Not using the logger here, because we may have had all sorts of init errors while setting it up.
        std::cerr << ex.what() << std::endl;
        return 99;
    }
    catch (std::exception &ex)
    {
        // Not using the logger here, because we may have had all sorts of init errors while setting it up.
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
