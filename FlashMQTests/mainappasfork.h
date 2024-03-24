#ifndef MAINAPPASFORK_H
#define MAINAPPASFORK_H

#include <vector>
#include <string>
#include <unistd.h>
#include "conffiletemp.h"

/**
 * @brief The MainAppAsFork class provides a way to run the FlashMQ server in a separate process for tests. This is convenient for isolation,
 * running multiple versions (bridging) and avoids conflicts with (thread) globals. But, it can also be inconvenient, because you
 * don't have access to the internal state (for assertions) and on abnormal exist the child processes may linger (although we
 * could devise stuff against that).
 */
class MainAppAsFork
{
    pid_t child = -1;
    std::vector<std::string> args;
    ConfFileTemp defaultConf;
public:
    static std::string getConfigFileFromArgs(const std::vector<std::string> &args);
    MainAppAsFork();
    MainAppAsFork(const std::vector<std::string> &args);
    ~MainAppAsFork();
    void start();
    void stop();
    void waitForStarted(int port=21883);
};

#endif // MAINAPPASFORK_H
