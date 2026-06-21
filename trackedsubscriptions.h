#ifndef TRACKEDSUBSCRIPTIONS_H
#define TRACKEDSUBSCRIPTIONS_H

#include <string>
#include <memory>
#include <set>
#include "utils.h"

enum class TrackedSubscriptionMutationTask
{
    Subscribe,
    Unsubscribe
};

struct TrackedSubscriptionMutation
{
    const std::string pattern;
    const uint8_t qos{};
    const std::string originatingClientId;
    const std::weak_ptr<Session> originatingSession;
    const std::optional<SubAckReleaseTrigger> subAckReleaseTrigger;
    const TrackedSubscriptionMutationTask task{};
    const std::chrono::time_point<std::chrono::steady_clock> createdAt {std::chrono::steady_clock::now()};

    TrackedSubscriptionMutation() = delete;
    FMQ_DISABLE_COPY(TrackedSubscriptionMutation);
    TrackedSubscriptionMutation(TrackedSubscriptionMutation&&) = default;
    TrackedSubscriptionMutation(
        const std::string &pattern, const uint8_t qos, const std::string &originatingClientId,
        const std::shared_ptr<Session> &originatingSession, const SubAckReleaseTrigger *subAckReleaseTrigger, TrackedSubscriptionMutationTask task);
};

class TrackedSubscription
{
    std::set<std::weak_ptr<Session>, std::owner_less<std::weak_ptr<Session>>> m_sessions;
    std::chrono::time_point<std::chrono::steady_clock> m_updated_at = std::chrono::steady_clock::now();

public:
    const std::string pattern;
    uint8_t qos{};

    TrackedSubscription() = delete;
    FMQ_DISABLE_COPY(TrackedSubscription);
    TrackedSubscription(const std::string &pattern, const uint8_t qos);

    void purge();
    void add_session(const std::weak_ptr<Session> &session);
    void remove_session(const std::weak_ptr<Session> &session);

    bool empty() const { return m_sessions.empty(); }
    std::chrono::time_point<std::chrono::steady_clock> updated_at() const { return m_updated_at;};
};

struct InFlightTrackedSubscription
{
    uint16_t id = 0;
    std::vector<Subscribe> subscribes;
    std::weak_ptr<Client> originating_client;
    int tryCount = 0;
    std::chrono::time_point<std::chrono::steady_clock> createdAt = std::chrono::steady_clock::now();

    bool outdated() const;
};

struct InFlightTrackedUnsubscription
{
    uint16_t id = 0;
    std::vector<Unsubscribe> unsubscribes;
    int tryCount = 0;
    std::chrono::time_point<std::chrono::steady_clock> createdAt = std::chrono::steady_clock::now();

    bool outdated() const;
};


#endif // TRACKEDSUBSCRIPTIONS_H
