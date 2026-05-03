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

    TrackedSubscriptionMutation() = delete;
    FMQ_DISABLE_COPY(TrackedSubscriptionMutation);
    TrackedSubscriptionMutation(TrackedSubscriptionMutation&&) = default;
    TrackedSubscriptionMutation(
        const std::string &pattern, const uint8_t qos, const std::string &originatingClientId,
        const std::shared_ptr<Session> &originatingSession, const SubAckReleaseTrigger *subAckReleaseTrigger, TrackedSubscriptionMutationTask task);
};

struct TrackedSubscription
{
    std::string pattern;
    uint8_t qos{};
    std::set<std::weak_ptr<Session>, std::owner_less<std::weak_ptr<Session>>> sessions;

    TrackedSubscription() = delete;
    FMQ_DISABLE_COPY(TrackedSubscription);
    TrackedSubscription(const std::string &pattern, const uint8_t qos);

    void purge();
    bool empty() const;
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
