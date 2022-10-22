/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
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

#include "sslctxmanager.h"
#include "listener.h"
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

    void testKeyValidity(const std::string &key, const std::set<std::string> &validKeys) const;
    void checkFileExistsAndReadable(const std::string &key, const std::string &pathToCheck, ssize_t max_size = std::numeric_limits<ssize_t>::max()) const;
    void checkFileOrItsDirWritable(const std::string &filepath) const;
public:
    ConfigFileParser(const std::string &path);
    void loadFile(bool test);

    const Settings &getSettings();
};

#endif // CONFIGFILEPARSER_H
