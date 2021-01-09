#include "configfileparser.h"

#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include "fstream"

#include "exceptions.h"
#include "utils.h"
#include <regex>


mosquitto_auth_opt::mosquitto_auth_opt(const std::string &key, const std::string &value)
{
    this->key = strdup(key.c_str());
    this->value = strdup(value.c_str());
}

mosquitto_auth_opt::mosquitto_auth_opt(mosquitto_auth_opt &&other)
{
    this->key = other.key;
    this->value = other.value;
    other.key = nullptr;
    other.value = nullptr;
}

mosquitto_auth_opt::~mosquitto_auth_opt()
{
    if (key)
        delete key;
    if (value)
        delete value;
}

AuthOptCompatWrap::AuthOptCompatWrap(const std::unordered_map<std::string, std::string> &authOpts)
{
    for(auto &pair : authOpts)
    {
        mosquitto_auth_opt opt(pair.first, pair.second);
        optArray.push_back(std::move(opt));
    }
}

ConfigFileParser::ConfigFileParser(const std::string &path) :
    path(path)
{
    validKeys.insert("auth_plugin");
    validKeys.insert("log_file");
}

void ConfigFileParser::loadFile()
{
    if (path.empty())
        return;

    if (access(path.c_str(), R_OK) != 0)
    {
        std::ostringstream oss;
        oss << "Error: " << path << " is not there or not readable";
        throw ConfigFileException(oss.str());
    }

    std::ifstream infile(path, std::ios::in);

    if (!infile.is_open())
    {
        std::ostringstream oss;
        oss << "Error loading " << path;
        throw ConfigFileException(oss.str());
    }

    std::list<std::string> lines;

    const std::regex r("^([a-zA-Z0-9_\\-]+) +([a-zA-Z0-9_\\-/\\.]+)$");

    // First parse the file and keep the valid lines.
    for(std::string line; getline(infile, line ); )
    {
        trim(line);

        if (startsWith(line, "#"))
            continue;

        if (line.empty())
            continue;

        std::smatch matches;

        if (!std::regex_search(line, matches, r) || matches.size() != 3)
        {
            std::ostringstream oss;
            oss << "Line '" << line << "' not in 'key value' format";
            throw ConfigFileException(oss.str());
        }

        lines.push_back(line);
    }

    authOpts.clear();
    authOptCompatWrap.reset();

    // Then once we know the config file is valid, process it.
    for (std::string &line : lines)
    {
        std::smatch matches;

        if (!std::regex_search(line, matches, r) || matches.size() != 3)
        {
            throw ConfigFileException("Config parse error at a point that should not be possible.");
        }

        std::string key = matches[1].str();
        const std::string value = matches[2].str();

        const std::string auth_opt_ = "auth_opt_";
        if (startsWith(key, auth_opt_))
        {
            key.replace(0, auth_opt_.length(), "");
            authOpts[key] = value;
        }
        else
        {
            auto valid_key_it = validKeys.find(key);
            if (valid_key_it == validKeys.end())
            {
                std::ostringstream oss;
                oss << "Config key '" << key << "' is not valid";
                throw ConfigFileException(oss.str());
            }

            if (key == "auth_plugin")
            {
                this->authPluginPath = value;
            }

            if (key == "log_file")
            {
                this->logPath = value;
            }
        }
    }

    authOptCompatWrap.reset(new AuthOptCompatWrap(authOpts));
}

AuthOptCompatWrap &ConfigFileParser::getAuthOptsCompat()
{
    return *authOptCompatWrap.get();
}



