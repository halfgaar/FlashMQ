#ifndef CONFIGFILEPARSER_H
#define CONFIGFILEPARSER_H

#include <string>
#include <set>
#include <unordered_map>
#include <vector>
#include <memory>
#include <list>

#include "sslctxmanager.h"
#include "listener.h"
#include "settings.h"

#define ABSOLUTE_MAX_PACKET_SIZE 268435461 // 256 MB + 5

enum class ConfigParseLevel
{
    Root,
    Listen
};

class ConfigFileParser
{
    const std::string path;
    std::set<std::string> validKeys;
    std::set<std::string> validListenKeys;

    void testKeyValidity(const std::string &key, const std::set<std::string> &validKeys) const;
    void checkFileExistsAndReadable(const std::string &key, const std::string &pathToCheck) const;
    void checkFileOrItsDirWritable(const std::string &filepath) const;
public:
    ConfigFileParser(const std::string &path);
    void loadFile(bool test);

    std::unique_ptr<Settings> settings;
};

#endif // CONFIGFILEPARSER_H
