/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <optional>
#include <stdexcept>
#include <sstream>

#include "types.h"

#define API __attribute__((visibility("default")))

// Because exceptions are (potentially) visible to plugins, we want to make sure to avoid symbol collisions, so using their own namespace.
namespace FlashMQ
{

/**
 * @brief The ProtocolError class is handled by the error handler in the worker threads and is used to make decisions about if and how
 * to inform a client and log the message.
 *
 * It's mainly meant for errors that can be communicated with MQTT packets.
 */
class API ProtocolError : public std::runtime_error
{
public:
    const ReasonCodes reasonCode;

    ProtocolError(const std::string &msg, ReasonCodes reasonCode = ReasonCodes::UnspecifiedError) : std::runtime_error(msg),
        reasonCode(reasonCode)
    {

    }
};

class API BadClientException : public std::runtime_error
{
    std::optional<int> mLogLevel;

public:
    BadClientException(const std::string &msg, int logLevel=-1) : std::runtime_error(msg)
    {
        if (logLevel >= 0)
            this->mLogLevel = logLevel;
    }

    std::optional<int> getLogLevel() const { return this->mLogLevel; }

};

class API NotImplementedException : public std::runtime_error
{
public:
    NotImplementedException(const std::string &msg) : std::runtime_error(msg) {}
};

class API FatalError : public std::runtime_error
{
public:
    FatalError(const std::string &msg) : std::runtime_error(msg) {}
};

class API ConfigFileException : public std::runtime_error
{
public:
    ConfigFileException(const std::string &msg) : std::runtime_error(msg) {}
    ConfigFileException(std::ostringstream oss) : std::runtime_error(oss.str()) {}
};

class API pluginException : public std::runtime_error
{
public:
    pluginException(const std::string &msg) : std::runtime_error(msg) {}
};

class API BadWebsocketVersionException : public std::runtime_error
{
public:
    BadWebsocketVersionException(const std::string &msg) : std::runtime_error(msg) {}
};

class API BadHttpRequest : public std::runtime_error
{
public:
    BadHttpRequest(const std::string &msg) : std::runtime_error(msg) {}
};

}

using namespace FlashMQ;

#endif // EXCEPTIONS_H
