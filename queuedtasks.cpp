#include "queuedtasks.h"
#include "logger.h"

bool QueuedTask::operator<(const QueuedTask &rhs) const
{
    return this->when < rhs.when;
}

QueuedTasks::QueuedTasks()
{

}

uint32_t QueuedTasks::addTask(std::function<void ()> f, uint32_t delayInMs)
{
    std::chrono::time_point<std::chrono::steady_clock> when = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayInMs);

    while(++nextId == 0) {}

    const uint32_t id = nextId;
    tasks[id] = f;

    QueuedTask t;
    t.id = id;
    t.when = when;

    queuedTasks.insert(t);

    next = std::min(next, when);

    return id;
}

void QueuedTasks::eraseTask(uint32_t id)
{
    tasks.erase(id);
}

uint32_t QueuedTasks::getTimeTillNext() const
{
    if (__builtin_expect(tasks.empty(), 1))
        return std::numeric_limits<uint32_t>::max();

    std::chrono::milliseconds x = std::chrono::duration_cast<std::chrono::milliseconds>(next - std::chrono::steady_clock::now());
    std::chrono::milliseconds y = std::max<std::chrono::milliseconds>(std::chrono::milliseconds(0), x);
    return y.count();
}

void QueuedTasks::performAll()
{
    next = std::chrono::time_point<std::chrono::steady_clock>::max();
    const auto now = std::chrono::steady_clock::now();

    auto _pos = queuedTasks.begin();
    while (_pos != queuedTasks.end())
    {
        auto pos = _pos;
        _pos++;

        const QueuedTask &t = *pos;

        if (t.when > now)
        {
            next = t.when;
            break;
        }

        const uint32_t id = t.id;
        queuedTasks.erase(pos);

        try
        {
            auto tpos = tasks.find(id);
            if (tpos != tasks.end())
            {
                auto f = tpos->second;
                tasks.erase(tpos); // TODO: allow repeatable tasks? It would require more expensive nextId generation.
                f();
            }
        }
        catch (std::exception &ex)
        {
            Logger *logger = Logger::getInstance();
            logger->logf(LOG_ERR, "Error in delayed task: %s", ex.what());
        }
    }
}


