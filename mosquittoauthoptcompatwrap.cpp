#include "mosquittoauthoptcompatwrap.h"

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

mosquitto_auth_opt::mosquitto_auth_opt(const mosquitto_auth_opt &other)
{
    this->key = strdup(other.key);
    this->value = strdup(other.value);
}

mosquitto_auth_opt::~mosquitto_auth_opt()
{
    if (key)
        delete key;
    if (value)
        delete value;
}

mosquitto_auth_opt &mosquitto_auth_opt::operator=(const mosquitto_auth_opt &other)
{
    if (key)
        delete key;
    if (value)
        delete value;

    this->key = strdup(other.key);
    this->value = strdup(other.value);

    return *this;
}

AuthOptCompatWrap::AuthOptCompatWrap(const std::unordered_map<std::string, std::string> &authOpts)
{
    for(auto &pair : authOpts)
    {
        mosquitto_auth_opt opt(pair.first, pair.second);
        optArray.push_back(std::move(opt));
    }
}

