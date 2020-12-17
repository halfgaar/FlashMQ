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


#endif // EXCEPTIONS_H
