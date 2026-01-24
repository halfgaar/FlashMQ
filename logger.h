/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include <mutex>
#include <queue>
#include <thread>
#include <sstream>
#include <optional>
#include <fstream>
#include "semaphore.h"

#include "flashmq_plugin.h"

enum class LogLevel
{
    Debug,
    Info,
    Notice,
    Warning,
    Error,
    None
};

int logSslError(const char *str, size_t len, void *u);

/**
 * @brief Use as a temporary, so don't give a name. This makes the stream gets logged immediately.
 */
class StreamToLog : public std::ostringstream
{
    int level = LOG_NOTICE;
public:
    StreamToLog(int level);
    ~StreamToLog();
};

class LogLine
{
    std::string line;
    bool alsoToStdOut;

public:
    LogLine(std::string &&line, bool alsoToStdOut);
    LogLine(const char *s, size_t len, bool alsoToStdOut);
    LogLine();
    LogLine(const LogLine &other) = delete;
    LogLine(LogLine &&other) = default;
    LogLine &operator=(LogLine &&other) = default;

    bool alsoLogToStdOut() const;
    const std::string &getLine() const { return line;} ;
};

class Logger
{
    std::string logPath;
    int curLogLevel = LOG_ERR | LOG_WARNING | LOG_NOTICE | LOG_INFO | LOG_SUBSCRIBE | LOG_UNSUBSCRIBE ;
    std::mutex logMutex;
    std::mutex startStopMutex;
    std::queue<LogLine> lines;
    sem_t linesPending;
    std::thread writerThread;
    bool running = true;
    std::fstream ofile;
    bool alsoLogToStd = true;
    bool reload = false;

    Logger();
    ~Logger();
    static std::string_view getLogLevelString(int level);
    void reOpen();
    void writeLog();
    static std::string getPrefix(int level);

public:
    static Logger *getInstance();
    void logstring(int level, const std::string &str);
    void logf(int level, const char *str, va_list args);
    void logf(int level, const char *str, ...);
    StreamToLog log(int level);
    bool wouldLog(int level) const;

    void queueReOpen();
    void noLongerLogToStd();

    void setLogPath(const std::string &path);
    void setFlags(LogLevel level, bool logSubscriptions, bool logPublishes);
    void setFlags(std::optional<bool> logDebug, std::optional<bool> quiet);

    void quit();
    void start();

};

#endif // LOGGER_H
