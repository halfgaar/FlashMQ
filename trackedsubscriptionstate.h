#ifndef TRACKEDSUBSCRIPTIONSTATE_H
#define TRACKEDSUBSCRIPTIONSTATE_H

#include <memory>
#include <deque>
#include <map>
#include "mutexowned.h"
#include "reentrantmap.h"
#include "trackedsubscriptions.h"

class BridgeState;

enum class ProcessTrackedSubscriptionMutationsModifier
{
    StartResending,
    Continue,
    Retry,
    FirstFinishResending // When reconnecting to a remote server, we're iterating over our tracked subscriptions: Finish that first.
};

enum class PurgeTrackedSubscriptionModifier
{
    Start,
    Continue,
    Retry,
};

class TrackedSubscriptionState
{
    struct ArmedSubackData
    {
        const std::weak_ptr<Client> m_client;
        const uint16_t m_packet_id {};

        ArmedSubackData(const std::weak_ptr<Client> &client, const uint16_t packet_id);
    };

    struct ArmedSuback
    {
        const std::shared_ptr<ArmedSubackData> m_d;
        const uint16_t m_packet_id {};

        ArmedSuback() = delete;
        ArmedSuback(const std::weak_ptr<Client> &client, const uint16_t packet_id);

        bool operator<(const ArmedSuback &other) const;
    };

    MutexOwned<std::deque<TrackedSubscriptionMutation>> trackedSubscriptionMutations;
    std::unique_ptr<ReentrantMap<std::string, TrackedSubscription>> trackedSubscriptions = std::make_unique<ReentrantMap<std::string, TrackedSubscription>>();

    ReentrantMap<std::string, TrackedSubscription>::iterator curPosResending;
    size_t resendCount = 0;
    size_t resendTotal = 0;

    ReentrantMap<std::string, TrackedSubscription>::iterator curPosPurging;
    size_t purgeCount = 0;
    size_t purgeTotal = 0;

    std::optional<InFlightTrackedSubscription> inFlightTrackedSubscriptions;
    std::optional<InFlightTrackedUnsubscription> inFlightTrackedUnsubscriptions;

    std::unordered_map<uint16_t, std::unordered_map<std::string, std::set<ArmedSuback>>> armedSubacks;
    std::multimap<std::chrono::time_point<std::chrono::steady_clock>, std::weak_ptr<ArmedSubackData>> armedSubacksTimeouts;

    void stageInFlightTrackedSubscriptions(std::vector<Subscribe> &&subscribes, uint16_t pack_id);
    void stageInFlightTrackedUnsubscriptions(std::vector<Unsubscribe> &&unsubscribes, uint16_t pack_id);
    void sendInFlightTrackedSubscriptions(Client *network_client);
    void sendInFlightTrackedUnsubscriptions(Client *network_client);
    void resetIterators();

public:
    std::unique_ptr<ReentrantMap<std::string, TrackedSubscription>> stealTrackedSubscriptions();
    void replaceTrackedSubscriptions(std::unique_ptr<ReentrantMap<std::string, TrackedSubscription>> &&val);
    bool addTrackedSubscriptionMutation(TrackedSubscriptionMutation &&mut);
    void processTrackedSubscriptionMutations(
        const std::shared_ptr<BridgeState> &bridgeState, const ProcessTrackedSubscriptionMutationsModifier modifier);
    void cleanupExpiredTrackedSubscriptions(
        const std::shared_ptr<BridgeState> &bridgeState, const PurgeTrackedSubscriptionModifier modifier);
    bool requiresProcessingTrackedSubscriptions();
    bool requiresContinuationOfPurging();
    void handledSubackActions(const uint16_t id);
    void sendArmedStagedSuback(const uint16_t id);
    void sendAllTimedOutStagedSubacks();
    void removeMatchingInFlightTrackedSubscriptions(uint16_t id);
    void removeMatchingInFlightTrackedUnsubscriptions(uint16_t id);
    bool hasOutdatedInFlightTrackedSubscriptions() const;
    bool hasOutdatedInFlightTrackedUnsubscriptions() const;
};

#endif // TRACKEDSUBSCRIPTIONSTATE_H
