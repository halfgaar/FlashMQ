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
    free(key);
    key = nullptr;
    free(value);
    value = nullptr;
}

mosquitto_auth_opt &mosquitto_auth_opt::operator=(const mosquitto_auth_opt &other)
{
    free(key);
    key = nullptr;

    free(value);
    value = nullptr;

    if (other.key)
        this->key = strdup(other.key);
    if (other.value)
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

