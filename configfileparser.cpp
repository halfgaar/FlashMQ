#include "configfileparser.h"

#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include "fstream"

#include "openssl/ssl.h"
#include "openssl/err.h"

#include "exceptions.h"
#include "utils.h"
#include <regex>

#include "logger.h"


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

void ConfigFileParser::checkFileAccess(const std::string &key, const std::string &pathToCheck) const
{
    if (access(pathToCheck.c_str(), R_OK) != 0)
    {
        std::ostringstream oss;
        oss << "Error for '" << key << "': " << pathToCheck << " is not there or not readable";
        throw ConfigFileException(oss.str());
    }
}

// Using a separate ssl context to test, because it's the easiest way to load certs and key atomitcally.
void ConfigFileParser::testSsl(const std::string &fullchain, const std::string &privkey, uint portNr) const
{
    if (portNr == 0)
        return;

    if (fullchain.empty() && privkey.empty())
        throw ConfigFileException("No privkey and fullchain specified.");

    if (fullchain.empty())
        throw ConfigFileException("No private key specified for fullchain");

    if (privkey.empty())
        throw ConfigFileException("No fullchain specified for private key");

    SslCtxManager sslCtx;
    if (SSL_CTX_use_certificate_file(sslCtx.get(), fullchain.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        ERR_print_errors_cb(logSslError, NULL);
        throw ConfigFileException("Error loading full chain " + fullchain);
    }
    if (SSL_CTX_use_PrivateKey_file(sslCtx.get(), privkey.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        ERR_print_errors_cb(logSslError, NULL);
        throw ConfigFileException("Error loading private key " + privkey);
    }
    if (SSL_CTX_check_private_key(sslCtx.get()) != 1)
    {
        ERR_print_errors_cb(logSslError, NULL);
        throw ConfigFileException("Private key and certificate don't match.");
    }
}

ConfigFileParser::ConfigFileParser(const std::string &path) :
    path(path)
{
    validKeys.insert("auth_plugin");
    validKeys.insert("log_file");
    validKeys.insert("listen_port");
    validKeys.insert("ssl_listen_port");
    validKeys.insert("fullchain");
    validKeys.insert("privkey");
}

void ConfigFileParser::loadFile(bool test)
{
    if (path.empty())
        return;

    checkFileAccess("application config file", path);

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

    std::string sslFullChainTmp;
    std::string sslPrivkeyTmp;

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
                oss << "Config key '" << key << "' is not valid. This error should have been cought before. Bug?";
                throw ConfigFileException(oss.str());
            }

            if (key == "auth_plugin")
            {
                checkFileAccess(key, value);
                if (!test)
                    this->authPluginPath = value;
            }

            if (key == "log_file")
            {
                checkFileAccess(key, value);
                if (!test)
                    this->logPath = value;
            }

            if (key == "fullchain")
            {
                checkFileAccess(key, value);
                sslFullChainTmp = value;
            }

            if (key == "privkey")
            {
                checkFileAccess(key, value);
                sslPrivkeyTmp = value;
            }

            try
            {
                // TODO: make this possible. There are many error cases to deal with, like bind failures, etc. You don't want to end up without listeners.
                if (key == "listen_port")
                {
                    uint listenportNew = std::stoi(value);
                    if (listenPort > 0 && listenPort != listenportNew)
                        throw ConfigFileException("Changing (ssl_)listen_port is not supported at this time.");
                    listenPort = listenportNew;
                }

                // TODO: make this possible. There are many error cases to deal with, like bind failures, etc. You don't want to end up without listeners.
                if (key == "ssl_listen_port")
                {
                    uint sslListenPortNew = std::stoi(value);
                    if (sslListenPort > 0 && sslListenPort != sslListenPortNew)
                        throw ConfigFileException("Changing (ssl_)listen_port is not supported at this time.");
                    sslListenPort = sslListenPortNew;
                }

            }
            catch (std::invalid_argument &ex)
            {
                throw ConfigFileException(ex.what());
            }
        }
    }

    testSsl(sslFullChainTmp, sslPrivkeyTmp, sslListenPort);
    this->sslFullchain = sslFullChainTmp;
    this->sslPrivkey = sslPrivkeyTmp;

    authOptCompatWrap.reset(new AuthOptCompatWrap(authOpts));
}

AuthOptCompatWrap &ConfigFileParser::getAuthOptsCompat()
{
    return *authOptCompatWrap.get();
}



