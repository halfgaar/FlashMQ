#ifndef THREADDATA_H
#define THREADDATA_H

#include <thread>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

#include "forward_declarations.h"

#include "client.h"
#include "subscriptionstore.h"
#include "utils.h"



class ThreadData
{
    std::unordered_map<int, Client_p> clients_by_fd;
    std::mutex clients_by_fd_mutex;
    std::shared_ptr<SubscriptionStore> subscriptionStore;
    std::unordered_set<Client_p> readyForDequeueing;
    std::mutex readForDequeuingMutex;

public:
    bool running = true;
    std::thread thread;
    int threadnr = 0;
    int epollfd = 0;
    int event_fd = 0;

    ThreadData(int threadnr, std::shared_ptr<SubscriptionStore> &subscriptionStore);

    void moveThreadHere(std::thread &&thread);
    void quit();
    void giveClient(Client_p client);
    Client_p getClient(int fd);
    void removeClient(Client_p client);
    void removeClient(int fd);
    std::shared_ptr<SubscriptionStore> &getSubscriptionStore();
    void wakeUpThread();
    void addToReadyForDequeuing(Client_p &client);
    std::unordered_set<Client_p> &getReadyForDequeueing() { return readyForDequeueing; }
    void clearReadyForDequeueing();
};

#endif // THREADDATA_H
