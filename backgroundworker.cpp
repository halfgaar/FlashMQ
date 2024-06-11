#include "backgroundworker.h"

#include "logger.h"

void BackgroundWorker::doWork()
{
    while (running)
    {
        executing_task = false;

        // TODO: semaphore/event/poll
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (!running)
            continue;

        {
            std::lock_guard<std::mutex> locker(task_mutex);
            if (this->tasks.empty())
                continue;
        }

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

BackgroundWorker::BackgroundWorker()
{

}

BackgroundWorker::~BackgroundWorker()
{
    this->running = false;

    if (t.joinable())
        t.join();
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

    std::lock_guard<std::mutex> locker(task_mutex);
    this->tasks.push_front(f);
}
