#include "timer.h"
#include "sys/eventfd.h"
#include "sys/epoll.h"
#include "unistd.h"

#include "utils.h"

void CallbackEntry::updateExectedAt()
{
    this->lastExecuted = currentMSecsSinceEpoch();
}

uint64_t CallbackEntry::getNextCallMs() const
{
    int64_t elapsedSinceLastCall = currentMSecsSinceEpoch() - lastExecuted;
    if (elapsedSinceLastCall < 0) // Correct for clock drift
        elapsedSinceLastCall = 0;

    int64_t newDelay = this->interval - elapsedSinceLastCall;
    if (newDelay < 0)
        newDelay = 0;
    return newDelay;
}

bool CallbackEntry::operator <(const CallbackEntry &other) const
{
    return this->getNextCallMs() < other.getNextCallMs();
}

Timer::Timer()
{
    fd = eventfd(0, EFD_NONBLOCK);
    epollfd = check<std::runtime_error>(epoll_create(999));

    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev));
}

Timer::~Timer()
{
    close(fd);
    close(epollfd);
}

void Timer::start()
{
    running = true;

    auto f = std::bind(&Timer::process, this);
    this->t = std::thread(f, this);

    pthread_t native = this->t.native_handle();
    pthread_setname_np(native, "Timer");
}

void Timer::stop()
{
    running = false;
    uint64_t one = 1;
    write(fd, &one, sizeof(uint64_t));
    t.join();
}

void Timer::addCallback(std::function<void ()> f, uint64_t interval_ms, const std::string &name)
{
    logger->logf(LOG_DEBUG, "Adding event '%s' to the timer.", name.c_str());

    CallbackEntry c;
    c.f = f;
    c.interval = interval_ms;
    c.name = name;
    callbacks.push_back(std::move(c));
    sortAndSetSleeptimeTillNext();
    wakeUpPoll();
}

void Timer::sortAndSetSleeptimeTillNext()
{
    std::sort(callbacks.begin(), callbacks.end());
    this->sleeptime = callbacks.front().getNextCallMs();
}

void Timer::process()
{
    struct epoll_event events[MAX_TIMER_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_TIMER_EVENTS);

    while (running)
    {
        logger->logf(LOG_DEBUG, "Timer sleeping for %d ms until event '%s' or callbacks are added.", sleeptime, callbacks.front().name.c_str());
        int num_fds = epoll_wait(this->epollfd, events, MAX_TIMER_EVENTS, sleeptime);

        if (!running)
            continue;

        if (num_fds < 0)
        {
            if (errno == EINTR)
                continue;
            logger->logf(LOG_ERR, "Waiting for timer fd error: %s", strerror(errno));
        }

        // If it was the eventfd, an action woke up the loop, and not a pending event.
        for (int i = 0; i < num_fds; i++)
        {
            int cur_fd = events[i].data.fd;

            if (cur_fd == this->fd)
            {
                uint64_t eventfd_value = 0;
                check<std::runtime_error>(read(fd, &eventfd_value, sizeof(uint64_t)));
            }

            continue;
        }

        CallbackEntry &c = callbacks.front();
        c.updateExectedAt();
        c.f();

        sortAndSetSleeptimeTillNext();
    }
}

void Timer::wakeUpPoll()
{
    if (!running)
        return;

    uint64_t one = 1;
    write(fd, &one, sizeof(uint64_t));
}

