#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>

#include "threaddata.h"

class ThreadData;

class Client
{
    int fd;

    // maybe read buffer?

    ThreadData &threadData;

public:
    Client(int fd, ThreadData &threadData);
    ~Client();

    int getFd() { return fd;}

};

typedef std::shared_ptr<Client> Client_p;

#endif // CLIENT_H
