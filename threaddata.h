#ifndef THREADDATA_H
#define THREADDATA_H

#include <thread>
#include "utils.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>

class ThreadData
{
public:
    std::thread thread;
    int threadnr = 0;
    int epollfd = 0;
    int event_fd = 0;

    ThreadData(int threadnr);

    void giveFdToEpoll(int fd);
};

#endif // THREADDATA_H
