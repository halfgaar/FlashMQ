#ifndef TIMER_H
#define TIMER_H

#include <functional>
#include <thread>
#include <list>

#include "logger.h"
#include "utils.h"

#define MAX_TIMER_EVENTS 32

struct CallbackEntry
{
    uint64_t lastExecuted = currentMSecsSinceEpoch(); // assume the first one executed to avoid instantly calling it.
    uint64_t interval = 0;
    std::function<void ()> f = nullptr;
    std::string name;

    void updateExectedAt();
    uint64_t getNextCallMs() const;
    bool operator <(const CallbackEntry &other) const;
};

// Simple timer that calls your callback. The callback is executed on the timer thread.
class Timer
{
    std::thread t;
    int epollfd = 0;
    int fd = 0;
    uint64_t sleeptime = 1000;
    int running = false;
    Logger *logger = Logger::getInstance();
    std::vector<CallbackEntry> callbacks;

    void sortAndSetSleeptimeTillNext();
    void process();
    void wakeUpPoll();
public:
    Timer();
    ~Timer();
    void start();
    void stop();
    void addCallback(std::function<void()> f, uint64_t interval_ms, const std::string &name);
};

#endif // TIMER_H
