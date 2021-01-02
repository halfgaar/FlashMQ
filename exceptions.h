#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <exception>
#include <stdexcept>

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
};

class AuthPluginException : public std::runtime_error
{
public:
    AuthPluginException(const std::string &msg) : std::runtime_error(msg) {}
};

#endif // EXCEPTIONS_H
