#include "globber.h"

#include <cstring>
#include <stdexcept>

GlobT::GlobT()
{
    std::memset(&result, 0, sizeof (glob_t));
}

GlobT::~GlobT()
{
    globfree(&result);
}

std::vector<std::string> Globber::getGlob(const std::string &pattern)
{
    GlobT result;

    const int rc = glob(pattern.c_str(), 0, nullptr, &result.result);

    if (!(rc == 0 || rc == GLOB_NOMATCH))
    {
        throw std::runtime_error("Glob failed");
    }

    std::vector<std::string> filenames;

    for(size_t i = 0; i < result.result.gl_pathc; ++i)
    {
        filenames.push_back(std::string(result.result.gl_pathv[i]));
    }

    return filenames;
}
