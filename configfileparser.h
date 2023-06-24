/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef CONFIGFILEPARSER_H
#define CONFIGFILEPARSER_H

#include <string>
#include <set>
#include <unordered_map>
#include <vector>
#include <memory>
#include <list>
#include <limits>

#include "settings.h"

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

    Settings settings;

    bool testKeyValidity(const std::string &key, const std::string &matchKey, const std::set<std::string> &validKeys) const;
    void static checkFileExistsAndReadable(const std::string &key, const std::string &pathToCheck, ssize_t max_size = std::numeric_limits<ssize_t>::max());
    void static checkFileOrItsDirWritable(const std::string &filepath);
public:
    ConfigFileParser(const std::string &path);
    void loadFile(bool test);

    const Settings &getSettings();
};

#endif // CONFIGFILEPARSER_H
