#ifndef CLIENTACCEPTQUEUE_H
#define CLIENTACCEPTQUEUE_H

#include <sys/eventfd.h>

#include <vector>

#include "mutexowned.h"
#include "fdmanaged.h"
#include "client.h"

struct ClientAcceptQueue
{
    MutexOwned<std::vector<std::shared_ptr<Client>>> clients;
    MutexOwned<std::vector<std::shared_ptr<BridgeState>>> bridges;
    FdManaged event_fd = FdManaged(eventfd(0, EFD_NONBLOCK));

    void wakeUp();
    void readFd();
    void giveClient(std::shared_ptr<Client> &&client);
    std::vector<std::shared_ptr<Client>> takeClients();
    void giveBridge(std::shared_ptr<BridgeState> &&bridge);
    std::vector<std::shared_ptr<BridgeState>> takeBridges();
};

#endif // CLIENTACCEPTQUEUE_H
