#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <mutex>

// Compatible with Mosquitto, for auth plugin compatability.
#define LOG_NONE 0x00
#define LOG_INFO 0x01
#define LOG_NOTICE 0x02
#define LOG_WARNING 0x04
#define LOG_ERR 0x08
#define LOG_DEBUG 0x10

class Logger
{
    static Logger *instance;
    int curLogLevel = LOG_DEBUG;
    std::mutex logMutex;
    FILE *file = nullptr;

    Logger();
    std::string getLogLevelString(int level) const;

public:
    static Logger *getInstance();
    void logf(int level, const char *str, va_list args);
    void logf(int level, const char *str, ...);

};

#endif // LOGGER_H
