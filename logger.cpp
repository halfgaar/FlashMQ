#include "logger.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <string.h>

#include "exceptions.h"

Logger *Logger::instance = nullptr;
std::string Logger::logPath = "";

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
    va_list valist;
    va_start(valist, str);
    this->logf(level, str, valist);
    va_end(valist);
}

void Logger::reOpen()
{
    std::lock_guard<std::mutex> locker(logMutex);

    if (file)
    {
        fclose(file);
        file = nullptr;
    }

    if (logPath.empty())
        return;

    if ((file = fopen(logPath.c_str(), "a")) == nullptr)
    {
        std::string msg(strerror(errno));
        throw ConfigFileException("Error opening logfile: " + msg);
    }
}

// I want all messages logged during app startup to also show on stdout/err, otherwise failure can look so silent. So, call this when the app started.
void Logger::noLongerLogToStd()
{
    alsoLogToStd = false;
}

void Logger::setLogPath(const std::string &path)
{
    Logger::logPath = path;
}

void Logger::logf(int level, const char *str, va_list valist)
{
    if (level > curLogLevel) // TODO: wrong: bitmap based
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

    if (this->file)
    {
        va_list valist2;
        va_copy(valist2, valist);
        vfprintf(this->file, logfmtstring, valist2);
        fprintf(this->file, "\n");
        fflush(this->file);
        va_end(valist2);
    }

    if (!this->file || alsoLogToStd)
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
}

int logSslError(const char *str, size_t len, void *u)
{
    Logger *logger = Logger::getInstance();
    logger->logf(LOG_ERR, str);
}
