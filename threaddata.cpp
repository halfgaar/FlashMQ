#include "threaddata.h"


ThreadData::ThreadData(int threadnr) :
    threadnr(threadnr)
{
    epollfd = check<std::runtime_error>(epoll_create(999));
    event_fd = eventfd(0, EFD_NONBLOCK);
}

void ThreadData::giveFdToEpoll(int fd)
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev));
}
