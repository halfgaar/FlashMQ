#ifndef ONEINSTANCELOCK_H
#define ONEINSTANCELOCK_H

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include "logger.h"

class OneInstanceLock
{
    int fd = -1;
    std::string lockFilePath;
    Logger *logger = Logger::getInstance();

public:
    OneInstanceLock();
    ~OneInstanceLock();
    void lock();
    void unlock();
};

#endif // ONEINSTANCELOCK_H
