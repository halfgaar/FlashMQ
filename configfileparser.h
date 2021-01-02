#ifndef CONFIGFILEPARSER_H
#define CONFIGFILEPARSER_H

#include <string>
#include <set>
#include <unordered_map>
#include <vector>
#include <memory>

struct mosquitto_auth_opt
{
    char *key = nullptr;
    char *value = nullptr;

    mosquitto_auth_opt(const std::string &key, const std::string &value);
    mosquitto_auth_opt(mosquitto_auth_opt &&other);
    mosquitto_auth_opt(const mosquitto_auth_opt &other) = delete;
    ~mosquitto_auth_opt();
};

struct AuthOptCompatWrap
{
    std::vector<struct mosquitto_auth_opt> optArray;

    AuthOptCompatWrap(const std::unordered_map<std::string, std::string> &authOpts);
    AuthOptCompatWrap(const AuthOptCompatWrap &other) = delete;
    AuthOptCompatWrap(AuthOptCompatWrap &&other) = delete;

    struct mosquitto_auth_opt *head() { return &optArray[0]; }
    int size() { return optArray.size(); }
};

class ConfigFileParser
{
    const std::string path;
    std::set<std::string> validKeys;
    std::unordered_map<std::string, std::string> authOpts;
    std::unique_ptr<AuthOptCompatWrap> authOptCompatWrap;
    std::string authPluginPath;

public:
    ConfigFileParser(const std::string &path);
    void loadFile();
    AuthOptCompatWrap &getAuthOptsCompat();

    std::string getAuthPluginPath() { return authPluginPath; }
};

#endif // CONFIGFILEPARSER_H
