#include "threaddata.h"


ThreadData::ThreadData(int threadnr, std::shared_ptr<SubscriptionStore> &subscriptionStore) :
    subscriptionStore(subscriptionStore),
    threadnr(threadnr)
{
    epollfd = check<std::runtime_error>(epoll_create(999));
    event_fd = eventfd(0, EFD_NONBLOCK);

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = event_fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, event_fd, &ev));
}

void ThreadData::giveClient(Client_p client)
{
    int fd = client->getFd();
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev));

    clients_by_fd[fd] = client;
}

Client_p ThreadData::getClient(int fd)
{
    return this->clients_by_fd[fd];
}

void ThreadData::removeClient(Client_p client)
{
    clients_by_fd.erase(client->getFd());
}

std::shared_ptr<SubscriptionStore> &ThreadData::getSubscriptionStore()
{
    return subscriptionStore;
}

void ThreadData::wakeUpThread()
{
    uint64_t one = 1;
    write(event_fd, &one, sizeof(uint64_t));
}

void ThreadData::addToReadyForDequeuing(Client_p &client)
{
    std::lock_guard<std::mutex> lock(readForDequeuingMutex);
    this->readyForDequeueing.insert(client);
}

void ThreadData::clearReadyForDequeueing()
{
    std::lock_guard<std::mutex> lock(readForDequeuingMutex);
    this->readyForDequeueing.clear();
}


