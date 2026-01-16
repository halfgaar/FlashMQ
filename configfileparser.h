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
#include <regex>

#include "settings.h"

enum class ConfigParseLevel
{
    Root,
    Listen,
    Bridge
};

template<typename T>
typename std::enable_if<std::is_signed<T>::value, long long>::type
value_to_int(const std::string &key, const std::string &value)
{
    try
    {
        size_t len = 0;
        long long newVal{std::stoll(value, &len)};
        if (len != value.length())
        {
            throw std::exception();
        }
        return newVal;
    }
    catch (std::exception &ex)
    {
        throw ConfigFileException(formatString("%s's value of '%s' can't be parsed to a number", key.c_str(), value.c_str()));
    }
}

template<typename T>
typename std::enable_if<std::is_unsigned<T>::value, long long unsigned>::type
value_to_int(const std::string &key, const std::string &value)
{
    try
    {
        size_t len = 0;
        long long unsigned newVal{std::stoull(value, &len)};
        if (len != value.length())
        {
            throw std::exception();
        }
        return newVal;
    }
    catch (std::exception &ex)
    {
        throw ConfigFileException(formatString("%s's value of '%s' can't be parsed to a number", key.c_str(), value.c_str()));
    }
}

template<typename T>
T value_to_int_ranged(const std::string &key, const std::string &value, const T min=std::numeric_limits<T>::min(), const T max=std::numeric_limits<T>::max())
{
    const auto newVal{value_to_int<T>(key, value)};
    if (newVal < min || newVal > max)
    {
        std::ostringstream oss;
        oss << "Value '" << value << "' out of range for '" << key << "', which must be between ";

        if (sizeof(T) == 1)
            oss << static_cast<int>(min);
        else
            oss << min;

        oss << " and ";

        if (sizeof(T) == 1)
            oss << static_cast<int>(max);
        else
            oss << max;

        throw ConfigFileException(oss.str());
    }
    return static_cast<T>(newVal);
}

class ConfigFileParser
{
    const std::string path;
    std::set<std::string> validKeys;
    std::set<std::string> validListenKeys;
    std::set<std::string> validBridgeKeys;

    const std::regex key_value_regex = std::regex("^([\\w\\-]+)\\s+(.+)$");
    const std::regex block_regex_start = std::regex("^([a-zA-Z0-9_\\-]+) *\\{$");
    const std::regex block_regex_end = std::regex("^\\}$");

    Settings settings;

    void static testCorrectNumberOfValues(const std::string &key, size_t expected_values, const std::vector<std::string> &values);
    bool testKeyValidity(const std::string &key, const std::string &matchKey, const std::set<std::string> &validKeys) const;

public:
    void static checkFileExistsAndReadable(const std::string &key, const std::string &pathToCheck, ssize_t max_size = std::numeric_limits<ssize_t>::max());
    void static checkFileOrItsDirWritable(const std::string &filepath);
    void static checkDirExists(const std::string &key, const std::string &dir);

    ConfigFileParser(const std::string &path);
    void loadFile(bool test);
    std::list<std::string> readFileRecursively(const std::string &path) const;

    const Settings &getSettings();
};

#endif // CONFIGFILEPARSER_H
