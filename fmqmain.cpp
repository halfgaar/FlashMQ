/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <iostream>
#include <signal.h>
#include <memory>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/opensslconf.h>

#include "mainapp.h"
#include "utils.h"
#include "exceptions.h"

MainApp *mainApp = nullptr;

void signal_handler(int signal)
{
    if (signal == SIGPIPE)
    {
        return;
    }
    if (signal == SIGHUP)
    {
        mainApp->queueConfigReload();
    }
    else if (signal == SIGUSR1)
    {
        mainApp->queueReopenLogFile();
    }
    else if (signal == SIGUSR2)
    {
        mainApp->queueMemoryTrim();
    }
    else if (signal == SIGTERM || signal == SIGINT)
    {
        mainApp->queueQuit();
    }
}

int register_signal_handers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof (struct sigaction));
    sa.sa_handler = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    for (int signal : {SIGHUP, SIGTERM, SIGINT, SIGUSR1, SIGUSR2})
    {
        if (sigaction(signal, &sa, nullptr) != 0)
        {
            Logger *logger = Logger::getInstance();
            logger->logf(LOG_ERR, "Error registering signal handlers");
            return -1;
        }
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

int fmqmain(int argc, char *argv[])
{
#ifndef OPENSSL_THREADS
    std::cerr << "Error: FlashMQ was compiled with an OpenSSL without thread support." << std::endl;
    exit(66);
#endif

    Logger *logger = nullptr;

    try
    {
        logger = Logger::getInstance();
        MainApp::initMainApp(argc, argv);
        mainApp = MainApp::getMainApp();
        check<std::runtime_error>(register_signal_handers());

        std::string sse = "without SSE support";
#ifdef __SSE4_2__
        sse = "with SSE4.2 support";
#endif
#ifdef NDEBUG
        logger->logf(LOG_NOTICE, "Starting FlashMQ version %s, release build %s.", FLASHMQ_VERSION, sse.c_str());
#else
        logger->logf(LOG_NOTICE, "Starting FlashMQ version %s, debug build %s.", FLASHMQ_VERSION, sse.c_str());
#endif
        mainApp->start();
        logger->quit();
    }
    catch (ConfigFileException &ex)
    {
        if (logger)
            logger->quit();

        // Not using the logger here, because we may have had all sorts of init errors while setting it up.
        std::cerr << ex.what() << std::endl;
        return 99;
    }
    catch (std::exception &ex)
    {
        if (logger)
            logger->quit();

        // Not using the logger here, because we may have had all sorts of init errors while setting it up.
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
