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
#include <functional>

#include "forward_declarations.h"

#include "client.h"
#include "subscriptionstore.h"
#include "utils.h"
#include "configfileparser.h"
#include "authplugin.h"
#include "logger.h"

typedef void (*thread_f)(ThreadData *);

class ThreadData
{
    std::unordered_map<int, Client_p> clients_by_fd;
    std::mutex clients_by_fd_mutex;
    std::shared_ptr<SubscriptionStore> subscriptionStore;
    Logger *logger;

    void reload(std::shared_ptr<Settings> settings);
    void wakeUpThread();
    void doKeepAliveCheck();
    void quit();

public:
    Settings settingsLocalCopy; // Is updated on reload, within the thread loop.
    AuthPlugin authPlugin;
    bool running = true;
    std::thread thread;
    int threadnr = 0;
    int epollfd = 0;
    int taskEventFd = 0;
    std::mutex taskQueueMutex;
    std::forward_list<std::function<void()>> taskQueue;

    ThreadData(int threadnr, std::shared_ptr<SubscriptionStore> &subscriptionStore, std::shared_ptr<Settings> settings);
    ThreadData(const ThreadData &other) = delete;
    ThreadData(ThreadData &&other) = delete;

    void start(thread_f f);

    void giveClient(Client_p client);
    Client_p getClient(int fd);
    void removeClient(Client_p client);
    void removeClient(int fd);
    std::shared_ptr<SubscriptionStore> &getSubscriptionStore();

    void initAuthPlugin();
    void queueReload(std::shared_ptr<Settings> settings);
    void queueDoKeepAliveCheck();
    void queueQuit();
    void waitForQuit();

};

#endif // THREADDATA_H
