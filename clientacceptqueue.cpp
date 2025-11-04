#include <unistd.h>
#include "utils.h"
#include "logger.h"
#include "clientacceptqueue.h"



void ClientAcceptQueue::wakeUp()
{
    uint64_t one = 1;
    check<std::runtime_error>(write(event_fd.get(), &one, sizeof(uint64_t)));
}

void ClientAcceptQueue::readFd()
{
    uint64_t eventfd_value = 0;
    if (read(event_fd.get(), &eventfd_value, sizeof(uint64_t)) < 0)
        Logger::getInstance()->log(LOG_ERROR) << "Error reading fd in ClientAcceptQueue: " << strerror(errno);
}

void ClientAcceptQueue::giveClient(std::shared_ptr<Client> &&client)
{
    bool wakeUpNeeded = true;

    {
        auto locked = clients.lock();
        wakeUpNeeded = locked->empty();
        locked->emplace_back(std::move(client)); // We must give up ownership here, to avoid calling the client destructor in the main thread.
    }

    if (wakeUpNeeded)
        wakeUp();

}

std::vector<std::shared_ptr<Client>> ClientAcceptQueue::takeClients()
{
    std::vector<std::shared_ptr<Client>> clientsTaken;

    {
        auto locked = clients.lock();
        clientsTaken = std::move(*locked);
        locked->clear();
    }

    return clientsTaken;

}

void ClientAcceptQueue::giveBridge(std::shared_ptr<BridgeState> &&bridge)
{
    auto locked = bridges.lock();
    locked->emplace_back(std::move(bridge)); // We must give up ownership here, to avoid calling the client destructor in the main thread.
}

std::vector<std::shared_ptr<BridgeState>> ClientAcceptQueue::takeBridges()
{
    std::vector<std::shared_ptr<BridgeState>> bridgesTaken;

    {
        auto locked = bridges.lock();
        bridgesTaken = std::move(*locked);
        locked->clear();
    }

    return bridgesTaken;
}
