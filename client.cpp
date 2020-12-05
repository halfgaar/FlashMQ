#include "client.h"

Client::Client(int fd, ThreadData &threadData) :
    fd(fd),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Client::~Client()
{
    epoll_ctl(threadData.epollfd, EPOLL_CTL_DEL, fd, NULL); // NOTE: the last NULL can cause crash on old kernels
    close(fd);
}
