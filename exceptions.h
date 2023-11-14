/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <exception>
#include <stdexcept>
#include <sstream>

#include "types.h"

/**
 * @brief The ProtocolError class is handled by the error handler in the worker threads and is used to make decisions about if and how
 * to inform a client and log the message.
 *
 * It's mainly meant for errors that can be communicated with MQTT packets.
 */
class ProtocolError : public std::runtime_error
{
public:
    const ReasonCodes reasonCode;

    ProtocolError(const std::string &msg, ReasonCodes reasonCode = ReasonCodes::UnspecifiedError) : std::runtime_error(msg),
        reasonCode(reasonCode)
    {

    }
};

class BadClientException : public std::runtime_error
{
public:
    BadClientException(const std::string &msg) : std::runtime_error(msg) {}
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

class pluginException : public std::runtime_error
{
public:
    pluginException(const std::string &msg) : std::runtime_error(msg) {}
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
