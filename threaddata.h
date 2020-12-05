#ifndef THREADDATA_H
#define THREADDATA_H

#include <thread>
#include "utils.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "client.h"
#include <map>

class Client;
typedef std::shared_ptr<Client> Client_p;

class ThreadData
{
    std::map<int, Client_p> clients_by_fd;

public:
    std::thread thread;
    int threadnr = 0;
    int epollfd = 0;
    int event_fd = 0;

    ThreadData(int threadnr);

    void giveClient(Client_p client);
    Client_p getClient(int fd);
    void removeClient(Client_p client);
};

#endif // THREADDATA_H
