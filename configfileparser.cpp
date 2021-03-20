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

#include "configfileparser.h"

#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include "fstream"
#include <regex>

#include "openssl/ssl.h"
#include "openssl/err.h"

#include "exceptions.h"
#include "utils.h"
#include "logger.h"


void ConfigFileParser::testKeyValidity(const std::string &key, const std::set<std::string> &validKeys) const
{
    auto valid_key_it = validKeys.find(key);
    if (valid_key_it == validKeys.end())
    {
        std::ostringstream oss;
        oss << "Config key '" << key << "' is not valid (here).";
        throw ConfigFileException(oss.str());
    }
}

void ConfigFileParser::checkFileExistsAndReadable(const std::string &key, const std::string &pathToCheck) const
{
    if (access(pathToCheck.c_str(), R_OK) != 0)
    {
        std::ostringstream oss;
        oss << "Error for '" << key << "': " << pathToCheck << " is not there or not readable";
        throw ConfigFileException(oss.str());
    }
}

void ConfigFileParser::checkFileOrItsDirWritable(const std::string &filepath) const
{
    if (access(filepath.c_str(), F_OK) == 0)
    {
        if (access(filepath.c_str(), W_OK) != 0)
        {
            std::string msg = formatString("File '%s' is there, but not writable", filepath.c_str());
            throw ConfigFileException(msg);
        }
        return;
    }
    std::string dirname = dirnameOf(filepath);

    if (access(dirname.c_str(), W_OK) != 0)
    {
        std::string msg = formatString("File '%s' is not there and can't be created, because '%s' is also not writable", filepath.c_str(), dirname.c_str());
        throw ConfigFileException(msg);
    }
}

ConfigFileParser::ConfigFileParser(const std::string &path) :
    path(path)
{
    validKeys.insert("auth_plugin");
    validKeys.insert("log_file");
    validKeys.insert("allow_unsafe_clientid_chars");
    validKeys.insert("allow_unsafe_username_chars");
    validKeys.insert("auth_plugin_serialize_init");
    validKeys.insert("auth_plugin_serialize_auth_checks");
    validKeys.insert("client_initial_buffer_size");
    validKeys.insert("max_packet_size");
    validKeys.insert("log_debug");
    validKeys.insert("log_subscriptions");

    validListenKeys.insert("port");
    validListenKeys.insert("protocol");
    validListenKeys.insert("fullchain");
    validListenKeys.insert("privkey");
    validListenKeys.insert("inet_protocol");
    validListenKeys.insert("inet4_bind_address");
    validListenKeys.insert("inet6_bind_address");

    settings.reset(new Settings());
}

void ConfigFileParser::loadFile(bool test)
{
    if (path.empty())
        return;

    checkFileExistsAndReadable("application config file", path);

    std::ifstream infile(path, std::ios::in);

    if (!infile.is_open())
    {
        std::ostringstream oss;
        oss << "Error loading " << path;
        throw ConfigFileException(oss.str());
    }

    std::list<std::string> lines;

    const std::regex key_value_regex("^([a-zA-Z0-9_\\-]+) +([a-zA-Z0-9_\\-/\\.:]+)$");
    const std::regex block_regex_start("^([a-zA-Z0-9_\\-]+) *\\{$");
    const std::regex block_regex_end("^\\}$");

    bool inBlock = false;
    std::ostringstream oss;
    int linenr = 0;

    // First parse the file and keep the valid lines.
    for(std::string line; getline(infile, line ); )
    {
        trim(line);
        linenr++;

        if (startsWith(line, "#"))
            continue;

        if (line.empty())
            continue;

        std::smatch matches;

        const bool blockStartMatch = std::regex_search(line, matches, block_regex_start);
        const bool blockEndMatch = std::regex_search(line, matches, block_regex_end);

        if ((blockStartMatch && inBlock) || (blockEndMatch && !inBlock))
        {
            oss << "Unexpected block start or end at line " << linenr << ": " << line;
            throw ConfigFileException(oss.str());
        }

        if (!std::regex_search(line, matches, key_value_regex) && !blockStartMatch && !blockEndMatch)
        {
            oss << "Line '" << line << "' invalid";
            throw ConfigFileException(oss.str());
        }

        if (blockStartMatch)
            inBlock = true;
        if (blockEndMatch)
            inBlock = false;

        lines.push_back(line);
    }

    if (inBlock)
    {
        throw ConfigFileException("Unclosed config block. Expecting }");
    }

    std::unordered_map<std::string, std::string> authOpts;

    ConfigParseLevel curParseLevel = ConfigParseLevel::Root;
    std::shared_ptr<Listener> curListener;
    std::unique_ptr<Settings> tmpSettings(new Settings);

    // Then once we know the config file is valid, process it.
    for (std::string &line : lines)
    {
        std::smatch matches;

        if (std::regex_match(line, matches, block_regex_start))
        {
            std::string key = matches[1].str();
            if (matches[1].str() == "listen")
            {
                curParseLevel = ConfigParseLevel::Listen;
                curListener.reset(new Listener);
            }
            else
            {
                throw ConfigFileException(formatString("'%s' is not a valid block.", key.c_str()));
            }

            continue;
        }
        else if (std::regex_match(line, matches, block_regex_end))
        {
            if (curParseLevel == ConfigParseLevel::Listen)
            {
                curListener->isValid();
                tmpSettings->listeners.push_back(curListener);
                curListener.reset();
            }

            curParseLevel = ConfigParseLevel::Root;
            continue;
        }

        std::regex_match(line, matches, key_value_regex);

        std::string key = matches[1].str();
        const std::string value = matches[2].str();

        try
        {
            if (curParseLevel == ConfigParseLevel::Listen)
            {
                testKeyValidity(key, validListenKeys);

                if (key == "protocol")
                {
                    if (value != "mqtt" && value != "websockets")
                        throw ConfigFileException(formatString("Protocol '%s' is not a valid listener protocol", value.c_str()));
                    curListener->websocket = value == "websockets";
                }
                else if (key == "port")
                {
                    curListener->port = std::stoi(value);
                }
                else if (key == "fullchain")
                {
                    curListener->sslFullchain = value;
                }
                if (key == "privkey")
                {
                    curListener->sslPrivkey = value;
                }
                if (key == "inet_protocol")
                {
                    if (value == "ip4")
                        curListener->protocol = ListenerProtocol::IPv4;
                    else if (value == "ip6")
                        curListener->protocol = ListenerProtocol::IPv6;
                    else if (value == "ip4_ip6")
                        curListener->protocol = ListenerProtocol::IPv46;
                    else
                        throw ConfigFileException(formatString("Invalid inet protocol: %s", value.c_str()));
                }
                if (key == "inet4_bind_address")
                {
                    curListener->inet4BindAddress = value;
                }
                if (key == "inet6_bind_address")
                {
                    curListener->inet6BindAddress = value;
                }

                continue;
            }


            const std::string auth_opt_ = "auth_opt_";
            if (startsWith(key, auth_opt_))
            {
                key.replace(0, auth_opt_.length(), "");
                authOpts[key] = value;
            }
            else
            {
                testKeyValidity(key, validKeys);

                if (key == "auth_plugin")
                {
                    checkFileExistsAndReadable(key, value);
                    tmpSettings->authPluginPath = value;
                }

                if (key == "log_file")
                {
                    checkFileOrItsDirWritable(value);
                    tmpSettings->logPath = value;
                }

                if (key == "allow_unsafe_clientid_chars")
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings->allowUnsafeClientidChars = tmp;
                }

                if (key == "allow_unsafe_username_chars")
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings->allowUnsafeUsernameChars = tmp;
                }

                if (key == "auth_plugin_serialize_init")
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings->authPluginSerializeInit = tmp;
                }

                if (key == "auth_plugin_serialize_auth_checks")
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings->authPluginSerializeAuthChecks = tmp;
                }

                if (key == "client_initial_buffer_size")
                {
                    int newVal = std::stoi(value);
                    if (!isPowerOfTwo(newVal))
                        throw ConfigFileException("client_initial_buffer_size value " + value + " is not a power of two.");
                    tmpSettings->clientInitialBufferSize = newVal;
                }

                if (key == "max_packet_size")
                {
                    int newVal = std::stoi(value);
                    if (newVal > ABSOLUTE_MAX_PACKET_SIZE)
                    {
                        std::ostringstream oss;
                        oss << "Value for max_packet_size " << newVal << "is higher than absolute maximum " << ABSOLUTE_MAX_PACKET_SIZE;
                        throw ConfigFileException(oss.str());
                    }
                    tmpSettings->maxPacketSize = newVal;
                }

                if (key == "log_debug")
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings->logDebug = tmp;
                }

                if (key == "log_subscriptions")
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings->logSubscriptions = tmp;
                }
            }
        }
        catch (std::invalid_argument &ex) // catch for the stoi()
        {
            throw ConfigFileException(ex.what());
        }
    }

    tmpSettings->authOptCompatWrap = AuthOptCompatWrap(authOpts);

    if (!test)
    {
        this->settings = std::move(tmpSettings);
    }
}

AuthOptCompatWrap &Settings::getAuthOptsCompat()
{
    return authOptCompatWrap;
}


