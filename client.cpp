#include "client.h"

Client::Client(int fd, ThreadData_p threadData) :
    fd(fd),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    readbuf = (char*)malloc(CLIENT_BUFFER_SIZE);
}

Client::~Client()
{
    epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL); // NOTE: the last NULL can cause crash on old kernels
    close(fd);
    free(readbuf);
}

// false means any kind of error we want to get rid of the client for.
bool Client::readFdIntoBuffer()
{
    int read_size = getMaxWriteSize();

    int n;
    while ((n = read(fd, &readbuf[wi], read_size)) != 0)
    {
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                return false;
        }

        wi += n;
        size_t bytesUsed = getBufBytesUsed();

        // TODO: we need a buffer to keep partial frames in, so/and can we reduce the size of this buffer again periodically?
        if (bytesUsed >= bufsize)
        {
            const size_t newBufSize = bufsize * 2;
            readbuf = (char*)realloc(readbuf, newBufSize);
            bufsize = newBufSize;
        }

        wi = wi % bufsize;
        read_size = getMaxWriteSize();
    }

    if (n == 0) // client disconnected.
    {
        return false;
    }

    return true;
}

void Client::writeTest()
{
    char *p = &readbuf[ri];
    size_t max_read = getMaxReadSize();
    ri = (ri + max_read) % bufsize;
    write(fd, p, max_read);
}











