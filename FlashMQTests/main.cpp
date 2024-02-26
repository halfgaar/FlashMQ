#include <iostream>
#include <vector>

#include "maintests.h"

int main(int argc, char *argv[])
{
    std::vector<std::string> tests;

    for (int i = 1; i < argc ; i++)
    {
        tests.push_back(std::string(argv[i]));
    }

    MainTests maintests;
    if (!maintests.test(tests))
        return 1;

    return 0;
}
