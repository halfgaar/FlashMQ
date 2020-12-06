#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>

#include "threaddata.h"

#define CLIENT_BUFFER_SIZE 16

class ThreadData;
typedef  std::shared_ptr<ThreadData> ThreadData_p;

class Client
{
    int fd;

    char *readbuf = NULL; // With many clients, it may not be smart to keep a (big) buffer around.
    size_t bufsize = CLIENT_BUFFER_SIZE;
    int wi = 0;
    int ri = 0;

    ThreadData_p threadData;

    size_t getBufBytesUsed()
    {
        size_t result = 0;
        if (wi >= ri)
            result = wi - ri;
        else
            result = (bufsize + wi) - ri;
    };

    size_t getMaxWriteSize()
    {
        size_t available = bufsize - getBufBytesUsed();
        size_t space_at_end = bufsize - wi;
        size_t answer = std::min<int>(available, space_at_end);
        return answer;
    }

    size_t getMaxReadSize()
    {
        size_t available = getBufBytesUsed();
        size_t space_to_end = bufsize - ri;
        size_t answer = std::min<int>(available, space_to_end);
        return answer;
    }

public:
    Client(int fd, ThreadData_p threadData);
    ~Client();

    int getFd() { return fd;}
    bool readFdIntoBuffer();
    void writeTest();

};

typedef std::shared_ptr<Client> Client_p;

#endif // CLIENT_H
