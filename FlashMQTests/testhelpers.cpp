#include "testhelpers.h"

#include <iostream>
#include <cstring>

int assert_count;
int assert_fail_count;
bool asserts_print;

bool fmq_assert(bool b, const char *failmsg, const char *actual, const char *expected, const char *file, int line)
{
    assert_count++;

    if (!b)
    {
        assert_fail_count++;

        if (asserts_print)
        {
            // There are two types of failmsg: unformatted ones and formatted ones.
            // By testing for a newline we can detect formatted ones.
            if (strchr(failmsg, '\n') == nullptr)
            {
                // unformatted
                std::cout << RED << "FAIL" << COLOR_END << ": '" << failmsg << "', " << actual << " != " << expected << std::endl
                          << " in " << file << ", line " << line << std::endl;
            }
            else
            {
                // formatted
                std::cout << RED << "FAIL" << COLOR_END << " in " << file << ", line " << line << std::endl;
                std::cout << failmsg << std::endl;
                std::cout << "Comparison: " << actual << " != " << expected << std::endl;
            }
        }
    }

    return b;
}

void fmq_fail(const char *failmsg, const char *file, int line)
{
    assert_count++;
    assert_fail_count++;
    if (asserts_print)
        std::cout << RED << "FAIL" << COLOR_END << ": " << failmsg << std::endl << " in " << file << ", line " << line << std::endl;
}
