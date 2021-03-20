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

#ifndef MOSQUITTOAUTHOPTCOMPATWRAP_H
#define MOSQUITTOAUTHOPTCOMPATWRAP_H

#include <vector>
#include <unordered_map>
#include <cstring>

/**
 * @brief The mosquitto_auth_opt struct is a resource managed class of auth options, compatible with passing as arguments to Mosquitto
 * auth plugins.
 *
 * It's fully assignable and copyable.
 */
struct mosquitto_auth_opt
{
    char *key = nullptr;
    char *value = nullptr;

    mosquitto_auth_opt(const std::string &key, const std::string &value);
    mosquitto_auth_opt(mosquitto_auth_opt &&other);
    mosquitto_auth_opt(const mosquitto_auth_opt &other);
    ~mosquitto_auth_opt();

    mosquitto_auth_opt& operator=(const mosquitto_auth_opt &other);
};

/**
 * @brief The AuthOptCompatWrap struct contains a vector of mosquitto auth options, with a head pointer and count which can be passed to
 * Mosquitto auth plugins.
 */
struct AuthOptCompatWrap
{
    std::vector<struct mosquitto_auth_opt> optArray;

    AuthOptCompatWrap(const std::unordered_map<std::string, std::string> &authOpts);
    AuthOptCompatWrap(const AuthOptCompatWrap &other) = default;
    AuthOptCompatWrap(AuthOptCompatWrap &&other) = delete;
    AuthOptCompatWrap() = default;

    struct mosquitto_auth_opt *head() { return &optArray[0]; }
    int size() { return optArray.size(); }

    AuthOptCompatWrap &operator=(const AuthOptCompatWrap &other) = default;
};


#endif // MOSQUITTOAUTHOPTCOMPATWRAP_H
