#ifndef GLOBBER_H
#define GLOBBER_H

#include <glob.h>
#include <string>
#include <vector>

class GlobT
{
    friend class Globber;

    glob_t result;

public:
    GlobT();
    ~GlobT();
};

class Globber
{
public:

    std::vector<std::string> getGlob(const std::string &pattern);
};

#endif // GLOBBER_H
