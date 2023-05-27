/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
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
    if (other.key)
        this->key = strdup(other.key);
    if (other.value)
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

