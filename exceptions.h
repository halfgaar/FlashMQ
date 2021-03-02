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
