/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "logger.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <string.h>
#include <functional>

#include "exceptions.h"
#include "utils.h"

LogLine::LogLine(std::string &&line, bool alsoToStdOut) :
    line(line),
    alsoToStdOut(alsoToStdOut)
{

}

LogLine::LogLine(const char *s, size_t len, bool alsoToStdOut) :
    line(s, len),
    alsoToStdOut(alsoToStdOut)
{

}

LogLine::LogLine() :
    alsoToStdOut(true)
{

}

const char *LogLine::c_str() const
{
    return line.c_str();
}

bool LogLine::alsoLogToStdOut() const
{
    return alsoToStdOut;
}

Logger::Logger()
{
    memset(&linesPending, 1, sizeof(sem_t));
    sem_init(&linesPending, 0, 0);

    auto f = std::bind(&Logger::writeLog, this);
    this->writerThread = std::thread(f, this);

    pthread_t native = this->writerThread.native_handle();
    pthread_setname_np(native, "LogWriter");
}

Logger::~Logger()
{
    if (running)
        quit();

    if (file)
    {
        fclose(file);
        file = nullptr;
    }

    sem_close(&linesPending);
}

std::string_view Logger::getLogLevelString(int level) const
{
    switch (level)
    {
    case LOG_NONE:
        return "NONE";
    case LOG_INFO:
        return "INFO";
    case LOG_NOTICE:
        return "NOTICE";
    case LOG_WARNING:
        return "WARNING";
    case LOG_ERR:
        return "ERROR";
    case LOG_DEBUG:
        return "DEBUG";
    case LOG_SUBSCRIBE:
        return "SUBSCRIBE";
    case LOG_UNSUBSCRIBE:
        return "UNSUBSCRIBE";
    default:
        return "UNKNOWN LOG LEVEL";
    }
}

Logger *Logger::getInstance()
{
    static Logger instance;
    return &instance;
}

void Logger::logf(int level, const char *str, ...)
{
    va_list valist;
    va_start(valist, str);
    this->logf(level, str, valist);
    va_end(valist);
}

void Logger::queueReOpen()
{
    reload = true;
    sem_post(&linesPending);
}

void Logger::reOpen()
{
    if (file)
    {
        fclose(file);
        file = nullptr;
    }

    if (logPath.empty())
        return;

    if ((file = fopen(logPath.c_str(), "a")) == nullptr)
    {
        logf(LOG_ERR, "(Re)opening log file '%s' error: %s. Logging to stdout.", logPath.c_str(), strerror(errno));
    }
}

// I want all messages logged during app startup to also show on stdout/err, otherwise failure can look so silent. So, call this when the app started.
void Logger::noLongerLogToStd()
{
    if (!logPath.empty())
        logf(LOG_INFO, "Switching logging from stdout to logfile '%s'", logPath.c_str());
    alsoLogToStd = false;
}

void Logger::setLogPath(const std::string &path)
{
    this->logPath = path;
}

void Logger::setFlags(bool logDebug, bool logSubscriptions, bool quiet)
{
    curLogLevel = LOG_ERR | LOG_WARNING | LOG_NOTICE | LOG_INFO | LOG_SUBSCRIBE | LOG_UNSUBSCRIBE ;

    if (logDebug)
        curLogLevel |= LOG_DEBUG;
    else
        curLogLevel &= ~LOG_DEBUG;

    if (logSubscriptions)
        curLogLevel |= (LOG_UNSUBSCRIBE | LOG_SUBSCRIBE);
    else
        curLogLevel &= ~(LOG_UNSUBSCRIBE | LOG_SUBSCRIBE);

    if (!quiet)
        curLogLevel |= (LOG_NOTICE | LOG_INFO);
    else
        curLogLevel &= ~(LOG_NOTICE | LOG_INFO);
}

void Logger::quit()
{
    running = false;
    sem_post(&linesPending);
    if (writerThread.joinable())
        writerThread.join();
}

void Logger::writeLog()
{
    maskAllSignalsCurrentThread();

    int graceCounter = 0;

    LogLine line;
    while(running || (!lines.empty() && graceCounter++ < 1000 ))
    {
        sem_wait(&linesPending);

        if (reload)
        {
            reOpen();
        }

        {
            std::lock_guard<std::mutex> locker(logMutex);

            if (lines.empty())
                continue;

            line = std::move(lines.front());
            lines.pop();
        }

        if (this->file)
        {
            if (fputs(line.c_str(), this->file) < 0 ||
                fputs("\n", this->file) < 0 ||
                fflush(this->file) != 0)
            {
                alsoLogToStd = true;
                fputs("Writing to log failed. Enabling stdout logger.", stderr);
            }
        }

        if (!this->file || line.alsoLogToStdOut())
        {
            FILE *output = stdout;
#ifdef TESTING
            output = stderr; // the stdout interfers with Qt test XML output, so using stderr.
#endif
            fputs(line.c_str(), output);
            fputs("\n", output);
            fflush(output);
        }
    }
}

void Logger::logf(int level, const char *str, va_list valist)
{
    if ((level & curLogLevel) == 0)
        return;

    time_t time = std::time(nullptr);
    struct tm tm = *std::localtime(&time);
    std::ostringstream oss;

    std::string str_(str);
    oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] [" << getLogLevelString(level) << "] " << str_;
    oss.flush();
    const std::string s = oss.str();
    const char *logfmtstring = s.c_str();

    constexpr const int buf_size = 512;

    char buf[buf_size + 1];
    buf[buf_size] = 0;

    va_list valist2;
    va_copy(valist2, valist);
    vsnprintf(buf, buf_size, logfmtstring, valist);
    size_t len = std::min<size_t>(buf_size, strlen(buf));
    LogLine line(buf, len, alsoLogToStd);
    va_end(valist2);

    {
        std::lock_guard<std::mutex> locker(logMutex);
        lines.push(std::move(line));
    }

    sem_post(&linesPending);
}

int logSslError(const char *str, size_t len, void *u)
{
    (void) u;

    std::string msg(str, len);

    Logger *logger = Logger::getInstance();
    logger->logf(LOG_ERR, msg.c_str());
    return 0;
}
