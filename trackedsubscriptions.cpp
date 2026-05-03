#include "trackedsubscriptions.h"
#include "session.h"

bool InFlightTrackedSubscription::outdated() const
{
    return this->createdAt + std::chrono::seconds(7) < std::chrono::steady_clock::now();
}

bool InFlightTrackedUnsubscription::outdated() const
{
    return this->createdAt + std::chrono::seconds(7) < std::chrono::steady_clock::now();
}

TrackedSubscriptionMutation::TrackedSubscriptionMutation(
        const std::string &pattern, const uint8_t qos, const std::string &originatingClientId,
        const std::shared_ptr<Session> &originatingSession, const SubAckReleaseTrigger *subAckReleaseTrigger, TrackedSubscriptionMutationTask task) :
    pattern(pattern),
    qos(qos),
    originatingClientId(originatingClientId),
    originatingSession(originatingSession),
    subAckReleaseTrigger(subAckReleaseTrigger ? *subAckReleaseTrigger : std::optional<SubAckReleaseTrigger>()),
    task(task)
{

}

TrackedSubscription::TrackedSubscription(const std::string &pattern, const uint8_t qos) :
    pattern(pattern),
    qos(qos)
{

}

void TrackedSubscription::purge()
{
    for (auto _ = sessions.begin(); _ != sessions.end();)
    {
        auto session_pos = _++;

        if (session_pos->expired())
            sessions.erase(session_pos);
    }
}

bool TrackedSubscription::empty() const
{
    return sessions.empty();
}


