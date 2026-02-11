#include <csignal>
#include <iostream>
#include <vector>

#include "maintests.h"

void printHelp(const std::string &arg0)
{
    std::cout << std::endl;
    std::cout << "Usage: " << arg0 << " [ --skip-tests-with-internet ] [ --skip-server-tests ] " << " <tests> " << std::endl;
}

void signal_noop(int signal)
{
    std::cout << "Signal NoOP (if you used save-core-on-sigvtalrm.gdb a core dump will now be created)" << std::endl;
}

int main(int argc, char *argv[])
{
    std::signal(SIGVTALRM, signal_noop);

    bool skip_tests_with_internet = false;
    bool skip_server_tests = false;
    std::vector<std::string> tests;
    bool option_list_terminated = false;

    for (int i = 1; i < argc ; i++)
    {
        const std::string name(argv[i]);

        if (option_list_terminated)
            tests.push_back(name);
        else if (name == "--")
            option_list_terminated = true;
        else if (name == "--help")
        {
            printHelp(argv[0]);
            return 1;
        }
        else if (name == "--skip-tests-with-internet")
            skip_tests_with_internet = true;
        else if (name == "--skip-server-tests")
            skip_server_tests = true;
        else if (name.find("--") == 0)
        {
            std::cerr << "Unknown argument " << name << std::endl;
            printHelp(argv[0]);
            return 1;
        }
        else
            tests.push_back(name);
    }

    MainTests maintests;
    if (!maintests.test(skip_tests_with_internet, skip_server_tests, tests))
        return 1;

    return 0;
}
