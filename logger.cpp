/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "logger.h"
#include <sstream>
#include <string.h>
#include <functional>
#include <mutex>

#include "threaddata.h"
#include "globals.h"
#include "threadglobals.h"
#include "utils.h"

LogLine::LogLine(std::string &&line, bool alsoToStdOut) :
    line(std::move(line)),
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

bool LogLine::alsoLogToStdOut() const
{
    return alsoToStdOut;
}

Logger::Logger()
{
    memset(&linesPending, 1, sizeof(sem_t));
    sem_init(&linesPending, 0, 0);

    start();
}

Logger::~Logger()
{
    if (running)
        quit();

    if (ofile.is_open())
    {
        ofile.close();
    }

    sem_close(&linesPending);
}

std::string_view Logger::getLogLevelString(int level)
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

    if (!instance.running)
        instance.start();

    return &instance;
}

void Logger::logf(int level, const char *str, ...)
{
    va_list valist;
    va_start(valist, str);
    this->logf(level, str, valist);
    va_end(valist);
}

/**
 * @brief Logger::log
 * @param level
 * @return a StreamToLog, that you're not suppose to name. When you don't, its destructor will log the stream.
 *
 * Allows logging like: logger->log(LOG_NOTICE) << "blabla: " << 1 << ".". The advantage is safety (printf crashes), and not forgetting printf arguments.
 *
 * Beware though: C++ streams chars as characters. When you have an uint8_t or int8_t that's also a char, and those need to be cast to int first. A good
 * solution needs to be devised.
 */
StreamToLog Logger::log(int level)
{
    return StreamToLog(level);
}

bool Logger::wouldLog(int level) const
{
    return static_cast<bool>(level & curLogLevel);
}

void Logger::queueReOpen()
{
    reload = true;
    sem_post(&linesPending);
}

void Logger::reOpen()
{
    reload = false;

    if (ofile.is_open())
    {
        ofile.close();
    }

    if (logPath.empty())
        return;

    ofile.open(logPath, std::ios::app | std::ios::out);

    if (!ofile.good())
    {
        ofile.close();
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

/**
 * @brief Logger::setFlags sets the log level based on a maximum desired level.
 * @param level Level based on the defines LOG_*.
 * @param logSubscriptions
 * @param quiet
 *
 * The log levels are mosquitto-compatible, and while the terminology is similar to the syslog standard, they are unfortunately
 * not in order of verbosity/priority. So, we use our own enum for the config setting, so that DEBUG is indeed more verbose than INFO.
 *
 * The subscriptions flag is still set explicitly, because you may want that irrespective of the log level.
 */
void Logger::setFlags(LogLevel level, bool logSubscriptions)
{
    curLogLevel = 0;

    if (level <= LogLevel::Debug)
        curLogLevel |= LOG_DEBUG;
    if (level <= LogLevel::Info)
        curLogLevel |= LOG_INFO;
    if (level <= LogLevel::Notice)
        curLogLevel |= LOG_NOTICE;
    if (level <= LogLevel::Warning)
        curLogLevel |= LOG_WARNING;
    if (level <= LogLevel::Error)
        curLogLevel |= LOG_ERR;

    if (logSubscriptions)
        curLogLevel |= (LOG_UNSUBSCRIBE | LOG_SUBSCRIBE);
    else
        curLogLevel &= ~(LOG_UNSUBSCRIBE | LOG_SUBSCRIBE);
}

/**
 * @brief Logger::setFlags is provided for backwards compatability
 * @param logDebug
 * @param quiet
 */
void Logger::setFlags(std::optional<bool> logDebug, std::optional<bool> quiet)
{
    if (logDebug)
    {
        if (logDebug.value())
            curLogLevel |= LOG_DEBUG;
        else
            curLogLevel &= ~LOG_DEBUG;
    }

    // It only makes sense to allow quiet to mute things in backward compatability mode, not enable LOG_NOTICE and LOG_INFO again.
    if (quiet)
    {
        if (quiet.value())
            curLogLevel &= ~(LOG_NOTICE | LOG_INFO);
    }
}

void Logger::quit()
{
    std::lock_guard<std::mutex> locker(startStopMutex);

    running = false;
    sem_post(&linesPending);
    if (writerThread.joinable())
        writerThread.join();
}

void Logger::start()
{
    std::lock_guard<std::mutex> locker(startStopMutex);

    if (writerThread.joinable())
        return;

    running = true;

    auto f = std::bind(&Logger::writeLog, this);
    this->writerThread = std::thread(f, this);

    pthread_t native = this->writerThread.native_handle();
    pthread_setname_np(native, "LogWriter");
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

        if (ofile.is_open())
        {
            ofile << line.getLine() << std::endl;

            if (!ofile.good())
            {
                std::cerr << "Writing to log failed. Closing file and enabling stdout logger" << std::endl;
                ofile.close();
            }
        }

        if (!(ofile.is_open() && ofile.good()) || line.alsoLogToStdOut())
        {
#ifdef TESTING
            std::cerr << line.getLine() << std::endl; // the stdout interfers with Qt test XML output, so using stderr.
#else
            std::cout << line.getLine() << std::endl;
#endif
        }
    }
}

std::string Logger::getPrefix(int level)
{
    thread_local static auto caller_thread_id = pthread_self();
    const auto td = ThreadGlobals::getThreadData();

    std::ostringstream oss;
    const std::string stamp = timestampWithMillis();
    oss << "[" << stamp << "] [" << getLogLevelString(level) << "] ";

    if (td)
    {
        oss << "[T " << td->threadnr << "] ";
    }
    else if (pthread_equal(caller_thread_id, globals->createdByThread))
    {
        oss << "[main] ";
    }
    else
    {
        std::array<char, 20> buf{};
        if (pthread_getname_np(caller_thread_id, buf.data(), buf.size()) == 0)
        {
            std::string tname(buf.data());
            oss << "[T " << tname << "] ";
        }
        else
        {
            oss << "[T custom] ";
        }
    }

    std::string result = oss.str();
    return result;
}

void Logger::logstring(int level, const std::string &str)
{
    if ((level & curLogLevel) == 0)
        return;

    std::string s = getPrefix(level);
    s.append(str);
    LogLine line(std::move(s), alsoLogToStd);

    {
        std::lock_guard<std::mutex> locker(logMutex);
        lines.push(std::move(line));
    }

    sem_post(&linesPending);
}

void Logger::logf(int level, const char *str, va_list valist)
{
    if ((level & curLogLevel) == 0)
        return;

    std::string s = getPrefix(level);
    s.append(str);
    const char *logfmtstring = s.c_str();

    constexpr const int buf_size = 512;

    char buf[buf_size + 1];
    buf[buf_size] = 0;

    va_list valist2;
    va_copy(valist2, valist);

    const int rc = vsnprintf(buf, buf_size, logfmtstring, valist2);
    va_end(valist2);
    if (rc < 0)
        return;
    size_t len = std::min<size_t>(buf_size, strlen(buf));
    LogLine line(buf, len, alsoLogToStd);

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
    logger->logstring(LOG_ERR, msg);
    return 0;
}

StreamToLog::StreamToLog(int level) :
    level(level)
{

}

StreamToLog::~StreamToLog()
{
    const std::string s = str();
    Logger *logger = Logger::getInstance();
    logger->logstring(this->level, s);
}
