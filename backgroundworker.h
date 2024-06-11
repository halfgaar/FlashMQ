#ifndef BACKGROUNDWORKER_H
#define BACKGROUNDWORKER_H


#include <thread>
#include <functional>
#include <list>
#include <mutex>

class BackgroundWorker
{
    std::thread t;
    bool running = true;
    bool executing_task = false;

    std::mutex task_mutex;
    std::list<std::function<void()>> tasks;

    void doWork();
public:
    BackgroundWorker();
    ~BackgroundWorker();

    void start();
    void stop();
    void waitForStop();
    void addTask(std::function<void()> f, bool only_if_idle);
};

#endif // BACKGROUNDWORKER_H
