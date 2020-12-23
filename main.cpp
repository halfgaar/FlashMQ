#include <iostream>
#include <signal.h>
#include <memory>
#include <string.h>
#include <sys/resource.h>

#include "mainapp.h"

MainApp *mainApp = MainApp::getMainApp();

static void signal_handler(int signal)
{
    if (signal == SIGPIPE)
    {
        return;
    }
    if (signal == SIGHUP)
    {

    }
    else if (signal == SIGTERM || signal == SIGINT)
    {
        mainApp->quit();
    }
    else
    {
        std::cerr << "Received signal " << signal << std::endl;
    }
}

int register_signal_handers()
{
    // Quick'n dirty temp. TODO properly, with config file, checks, etc
    rlim_t rlim = 1000000;
    printf("Setting ulimit nofile to %ld.\n", rlim);
    struct rlimit v = { rlim, rlim };
    setrlimit(RLIMIT_NOFILE, &v);

    struct sigaction sa;
    memset(&sa, 0, sizeof (struct sigaction));
    sa.sa_handler = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGHUP, &sa, nullptr) != 0 || sigaction(SIGTERM, &sa, nullptr) != 0 || sigaction(SIGINT, &sa, nullptr) != 0)
    {
        std::cerr << "Error registering signals" << std::endl;
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

int main()
{
    try
    {
        check<std::runtime_error>(register_signal_handers());
        mainApp->start();
    }
    catch (std::exception &ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
