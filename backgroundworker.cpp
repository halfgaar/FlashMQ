#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "backgroundworker.h"

#include "logger.h"


void BackgroundWorker::doWork()
{
    struct pollfd fds[1];
    memset(fds, 0, sizeof(struct pollfd));
    fds[0].fd = wakeup_fd;
    fds[0].events = POLLIN;

    while (running)
    {
        executing_task = false;

        int fd_count = poll(fds, 1, 1000);

        if (fd_count == 0)
            continue;

        if (fd_count < 0)
        {
            Logger::getInstance()->log(LOG_ERR) << "poll() error in BackgroundWorker: " << strerror(errno);
            continue;
        }

        if (fds[0].revents & POLLIN)
        {
            uint64_t _;
            if (read(fds[0].fd, &_, sizeof(uint64_t)) < 0)
            {
                Logger::getInstance()->log(LOG_ERR) << "Error while reading from wakeup_fd: " << strerror(errno);
            }
        }

        if (!running)
            continue;

        std::list<std::function<void()>> copied_tasks;

        {
            std::lock_guard<std::mutex> locker(task_mutex);
            copied_tasks = std::move(this->tasks);
            this->tasks.clear();
        }

        for(auto &f : copied_tasks)
        {
            executing_task = true;

            try
            {
                f();
            }
            catch (std::exception &ex)
            {
                Logger *logger = Logger::getInstance();
                logger->log(LOG_ERR) << "Error in BackgroundWorker::do_work: " << ex.what();
            }
        }
    }
}

void BackgroundWorker::wake_up_thread()
{
    uint64_t one = 1;
    if (write(wakeup_fd, &one, sizeof(uint64_t)) < 0)
    {
        Logger::getInstance()->log(LOG_ERR) << "BackgroundWorker::wake_up_thread: " << strerror(errno);
    }
}

BackgroundWorker::BackgroundWorker()
{
    wakeup_fd = eventfd(0, EFD_NONBLOCK);

    if (wakeup_fd < 0)
    {
        throw std::runtime_error("Failed to initialize eventfd in background worker: " + std::string(strerror(errno)));
    }
}

BackgroundWorker::~BackgroundWorker()
{
    this->stop();

    if (t.joinable())
        t.join();

    if (wakeup_fd >= 0)
    {
        close(wakeup_fd);
        wakeup_fd = -1;
    }
}

void BackgroundWorker::start()
{
    std::lock_guard<std::mutex> locker(task_mutex);

    if (t.joinable())
        return;

    auto f = std::bind(&BackgroundWorker::doWork, this);
    t = std::thread(f);

    pthread_t native = this->t.native_handle();
    pthread_setname_np(native, "BgTasks");
}

void BackgroundWorker::stop()
{
    this->running = false;
    this->wake_up_thread();
}

void BackgroundWorker::waitForStop()
{
    if (t.joinable())
        t.join();
}

void BackgroundWorker::addTask(std::function<void ()> f, bool only_if_idle)
{
    if (only_if_idle && executing_task)
        return;

    {
        std::lock_guard<std::mutex> locker(task_mutex);
        this->tasks.push_front(f);
    }

    wake_up_thread();
}
