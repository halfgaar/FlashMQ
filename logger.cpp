#include "logger.h"
#include <ctime>
#include <sstream>
#include <iomanip>

Logger *Logger::instance = nullptr;

Logger::Logger()
{

}

std::string Logger::getLogLevelString(int level) const
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
    default:
        return "UNKNOWN LOG LEVEL";
    }
}

Logger *Logger::getInstance()
{
    if (instance == nullptr)
        instance = new Logger();
    return instance;
}

void Logger::logf(int level, const char *str, ...)
{
    if (level > curLogLevel)
        return;

    std::lock_guard<std::mutex> locker(logMutex);

    time_t time = std::time(nullptr);
    struct tm tm = *std::localtime(&time);
    std::ostringstream oss;

    std::string str_(str);
    oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] [" << getLogLevelString(level) << "] " << str_;
    oss.flush();
    const std::string s = oss.str();
    const char *logfmtstring = s.c_str();

    va_list valist;
    va_start(valist, str);
    if (this->file)
    {
        vfprintf(this->file, logfmtstring, valist);
        fprintf(this->file, "\n");
        fflush(this->file);
    }
    else
    {
#ifdef TESTING
        vfprintf(stderr, logfmtstring, valist); // the stdout interfers with Qt test XML output, so using stderr.
        fprintf(stderr, "\n");
        fflush(stderr);
#else
        vfprintf(stdout, logfmtstring, valist);
        fprintf(stdout, "\n");
        fflush(stdout);
#endif
    }
    va_end(valist);
}
