#ifndef LAZYSUBSCRIPTIONS_H
#define LAZYSUBSCRIPTIONS_H

#include <memory>
#include <vector>

#include "sharedmutexowned.h"
#include "bridgeconfig.h"
#include "utils.h"

struct LazySubscriber
{
    const std::weak_ptr<BridgeState> bridge;
    const std::shared_ptr<ThreadData> thread;
    const int min_required_wildcard_depth = std::numeric_limits<int>::max();
    const uint8_t qos = 2;
    const std::string share_name;

    LazySubscriber() = delete;
    LazySubscriber(
        const std::shared_ptr<BridgeState> &bridgeState, const std::shared_ptr<ThreadData> &thread,
        int min_required_wildcard_depth, uint8_t qos, const std::string &share_name);
};

struct LazySubscriptionNode
{
    SharedMutexOwned<std::vector<LazySubscriber>> subscribers;
    std::unordered_map<std::string, std::shared_ptr<LazySubscriptionNode>> children;

    LazySubscriptionNode() = default;
    FMQ_DISABLE_COPY_AND_MOVE(LazySubscriptionNode);
};

struct ReceivingLazySubscriber
{
    std::weak_ptr<BridgeState> receiver;
    std::shared_ptr<ThreadData> thread;
    const uint8_t qos = 0;
    const std::string share_name;
    const size_t depth = 0;
public:
    ReceivingLazySubscriber() = delete;
    ReceivingLazySubscriber(
        const std::weak_ptr<BridgeState> &receiver, const std::shared_ptr<ThreadData> &thread, uint8_t qos, const std::string &share_name,
        size_t depth);

    std::string getPatternWithShareName(const std::vector<std::string> &subtopics) const;
};

/**
 * Lazy subscriptions are a type of subscriptions that bridges can 'virtually' place on the remote end. It doesn't
 * actually place a subscription, but we will keep track of it, and relay matching incoming subscriptions to
 * the other end. This allows us to obtain all traffic from the other end that anybody may be interested in,
 * without actually getting all traffic of a particular sub tree.
 */
class LazySubscriptions
{
    SharedMutexOwned<const std::shared_ptr<LazySubscriptionNode>> root = std::make_shared<LazySubscriptionNode>();

    std::shared_ptr<LazySubscriptionNode> getDeepestNode(const std::vector<std::string> &subtopics);

    static void collectClientsEndpoint(
        LazySubscriptionNode *this_node, std::vector<ReceivingLazySubscriber> &collected_clients, const int level_depth, int first_wildcard_depth);
    static void collectClients(
        std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
        LazySubscriptionNode *this_node, std::vector<ReceivingLazySubscriber> &collected_clients, int level_depth, int first_wildcard_depth);

public:
    FMQ_DISABLE_COPY_AND_MOVE(LazySubscriptions);
    LazySubscriptions();

    void addSubscription(
        const std::shared_ptr<BridgeState> &bridgeState, const std::string &pattern,
        uint8_t qos, const std::string &share_name);
    void expandLazySubscriptions(
        TrackedSubscriptionMutationTask task, const std::shared_ptr<Session> originating_session,
        const std::vector<std::string> &subtopics, const uint8_t qos);
};

void registerLazySubscriptions(std::shared_ptr<BridgeState> &bridgeState);

#endif // LAZYSUBSCRIPTIONS_H
