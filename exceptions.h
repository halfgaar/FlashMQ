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

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <exception>
#include <stdexcept>
#include <sstream>

class ProtocolError : public std::runtime_error
{
public:
    ProtocolError(const std::string &msg) : std::runtime_error(msg) {}
};

class NotImplementedException : public std::runtime_error
{
public:
    NotImplementedException(const std::string &msg) : std::runtime_error(msg) {}
};

class FatalError : public std::runtime_error
{
public:
    FatalError(const std::string &msg) : std::runtime_error(msg) {}
};

class ConfigFileException : public std::runtime_error
{
public:
    ConfigFileException(const std::string &msg) : std::runtime_error(msg) {}
    ConfigFileException(std::ostringstream oss) : std::runtime_error(oss.str()) {}
};

class AuthPluginException : public std::runtime_error
{
public:
    AuthPluginException(const std::string &msg) : std::runtime_error(msg) {}
};

class BadWebsocketVersionException : public std::runtime_error
{
public:
    BadWebsocketVersionException(const std::string &msg) : std::runtime_error(msg) {}
};

class BadHttpRequest : public std::runtime_error
{
public:
    BadHttpRequest(const std::string &msg) : std::runtime_error(msg) {}
};

#endif // EXCEPTIONS_H
