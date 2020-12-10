#ifndef THREADDATA_H
#define THREADDATA_H

#include <thread>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <map>

#include "forward_declarations.h"

#include "client.h"
#include "subscriptionstore.h"
#include "utils.h"



class ThreadData
{
    std::map<int, Client_p> clients_by_fd;
    std::shared_ptr<SubscriptionStore> subscriptionStore;

public:
    std::thread thread;
    int threadnr = 0;
    int epollfd = 0;
    int event_fd = 0;

    ThreadData(int threadnr, std::shared_ptr<SubscriptionStore> &subscriptionStore);

    void giveClient(Client_p client);
    Client_p getClient(int fd);
    void removeClient(Client_p client);
    std::shared_ptr<SubscriptionStore> &getSubscriptionStore();
};

#endif // THREADDATA_H
