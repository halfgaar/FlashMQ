#include <utility>
#include "lazysubscriptions.h"
#include "threaddata.h"
#include "globals.h"

LazySubscriber::LazySubscriber(
        const std::shared_ptr<BridgeState> &bridgeState, const std::shared_ptr<ThreadData> &thread,
        const std::string &distribution_group_id, int min_required_wildcard_depth, uint8_t qos) :
    bridge(bridgeState),
    thread(thread),
    distribution_group_id(distribution_group_id),
    min_required_wildcard_depth(min_required_wildcard_depth),
    qos(qos)
{

}

LazySubscriptionNode::LazySubscriptionNode()
{

}



ReceivingLazySubscriber::ReceivingLazySubscriber(
        const std::weak_ptr<BridgeState> &receiver, const std::weak_ptr<ThreadData> &thread,
        const std::string &distribution_group, uint8_t qos, size_t depth) :
    receiver(receiver),
    thread(thread),
    distribution_group(distribution_group),
    qos(qos),
    depth(depth)
{

}

std::string ReceivingLazySubscriber::getPattern(const std::vector<std::string> &subtopics) const
{
    std::string pattern;

    /*
     * We use shares for when the connection count of bridges is changed and config is reloaded. This changes
     * the targetted client, and we need to make sure we still only get the message once. Also
     * see collectClientsEndpoint(...).
     */
    if (!distribution_group.empty())
    {
        pattern.append(std::string_view("$share/"));
        pattern.append(distribution_group);
    }

    size_t len = 0;
    for (const std::string &s : subtopics)
    {
        len++;

        if (!pattern.empty())
            pattern.append("/");

        if (len > this->depth)
        {
            pattern.append("#");
            break;
        }

        pattern.append(s);
    }

    return pattern;
}

std::shared_ptr<LazySubscriptionNode> LazySubscriptions::getDeepestNode(const std::vector<std::string> &subtopics)
{
    bool retry_mode = false;

    for(int i = 0; i < 2; i++)
    {
        assert(i < 1 || retry_mode);

        auto rlocked = root.shared_lock(std::defer_lock);
        auto wlocked = root.unique_lock(std::defer_lock);

        std::shared_ptr<LazySubscriptionNode> deepestNode;

        if (retry_mode)
        {
            wlocked.lock();
            deepestNode = *wlocked;
        }
        else
        {
            rlocked.lock();
            deepestNode = *rlocked;
        }

        assert(deepestNode);

        auto subtopic_pos = subtopics.begin();

        while(subtopic_pos != subtopics.end())
        {
            const std::string &subtopic = *subtopic_pos;
            std::shared_ptr<LazySubscriptionNode> *selectedChildren = nullptr;

            if (retry_mode)
            {
                assert(wlocked.owns_lock());
                selectedChildren = &deepestNode->children[subtopic];
            }
            else // read-only path
            {
                assert(rlocked.owns_lock());
                auto child_pos = deepestNode->children.find(subtopic);

                if (child_pos == deepestNode->children.cend())
                {
                    retry_mode = true;
                    break;
                }
                else
                {
                    selectedChildren = &child_pos->second;
                }
            }

            std::shared_ptr<LazySubscriptionNode> &node = *selectedChildren;

            if (!node)
            {
                if (!retry_mode)
                {
                    assert(rlocked.owns_lock());
                    retry_mode = true;
                    break;
                }

                assert(retry_mode);
                assert(wlocked.owns_lock());
                node = std::make_shared<LazySubscriptionNode>();
            }

            deepestNode = node;
            subtopic_pos++;
        }

        assert(deepestNode);

        if (retry_mode && subtopic_pos != subtopics.end())
            continue;

        return deepestNode;
    }

    throw std::runtime_error("Bug: LazySubscriptions::getDeepestNode() should have obtained a node after two passes.");
}




LazySubscriptions::LazySubscriptions()
{

}

void LazySubscriptions::addSubscription(
    const std::shared_ptr<BridgeState> &bridgeState, const std::string &pattern,
    uint8_t qos, const std::string &distribution_group_name)
{
    assert(pattern.find("$share") == std::string::npos);

    const std::vector<std::string> subtopics = splitTopic(pattern);

    if (std::any_of(subtopics.begin(), subtopics.end(), [](const std::string &s) { return s == "+";}))
    {
        throw std::runtime_error("Lazy subscriptions can't have + wildcards");
    }

    if (subtopics.empty() || subtopics.back() != "#")
    {
        throw std::runtime_error("Lazy subscription must end with # wildcard.");
    }

    const int min_required_wildcard_depth{getFirstWildcardDepth(subtopics) + 1};

    const std::shared_ptr<LazySubscriptionNode> node = getDeepestNode(subtopics);
    auto subscribers = node->subscriber_groups.unique_lock();
    assert(!bridgeState->threadData->expired());
    subscribers->operator[](bridgeState->c.getUnmultipliedClientIdPrefix()).emplace_back(
        bridgeState, bridgeState->threadData->lock(), distribution_group_name, min_required_wildcard_depth, qos);
    bridgeState->constructTrackedSubscriptions();
}

/**
 * @brief LazySubscriptions::collectClientsEndpoint
 *
 * Targetting the client based on the uniqueness of the resulting subscription. Because we always shorten the incoming
 * subscription to the broadest possible multi-level wildcard, it should not be possible for clients to
 * generate subscriptions resulting in different forwarded subscriptions. In other words, no matter if one or
 * multiple clients place overlapping subscriptions, they should always pick the same connection.
 */
void LazySubscriptions::collectClientsEndpoint(
    LazySubscriptionNode *this_node, const size_t previous_nodes_hash, std::vector<ReceivingLazySubscriber> &collected_clients,
    const std::optional<std::string> &originating_fmq_client_group_id, const int level_depth, int first_wildcard_depth) noexcept
{
    auto subscriber_data = this_node->subscriber_groups.shared_lock();

    for (const auto &pair : *subscriber_data)
    {
        const std::vector<LazySubscriber> &lazy_subscribers = pair.second;

        if (lazy_subscribers.empty())
            continue;

        /*
         * This mechanism, selecting by modulo of size, would normally have the problem that when you add connections
         * to your bridge and reload, it targets another connection, resulting in a new outgoing subscription. We
         * solve that by using shared subscriptions. See ReceivingLazySubscriber::getPattern(...).
         */
        const size_t start = previous_nodes_hash % lazy_subscribers.size();
        for (size_t i = 0; i < lazy_subscribers.size(); i++)
        {
            const LazySubscriber &s = lazy_subscribers.at((start + i) % lazy_subscribers.size());

            if (first_wildcard_depth > -1 && first_wildcard_depth < s.min_required_wildcard_depth)
                continue;

            auto &candidate = collected_clients.emplace_back(s.bridge, s.thread, s.distribution_group_id, s.qos, level_depth);
            const std::shared_ptr<BridgeState> bridge_candidate = candidate.receiver.lock();

            if (bridge_candidate)
            {
                if (bridge_candidate->c.getFmqClientGroupId() == originating_fmq_client_group_id)
                    collected_clients.pop_back();

                break;
            }

            collected_clients.pop_back();
        }
    }
}

void LazySubscriptions::collectClients(
    std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
    LazySubscriptionNode *this_node, const size_t previous_nodes_hash, std::vector<ReceivingLazySubscriber> &collected_clients,
    const std::optional<std::string> &originating_fmq_client_group_id, int level_depth, int first_wildcard_depth) noexcept
{
    if (this_node == nullptr)
        return;


    const auto &children = std::as_const(this_node->children);

    if (cur_subtopic_it == end)
    {
        // Incoming subscriptions should always be shorter than our configured lazy subscription, so
        // if we run out of 'pattern', there's nothing left to do.
        return;
    }

    if (this_node->children.empty())
        return;

    level_depth++;
    const std::string &cur_subtop = *cur_subtopic_it;
    const size_t next_hash_of_tail = previous_nodes_hash ^ std::hash<std::string>()(cur_subtop);
    const auto next_subtopic = ++cur_subtopic_it;
    const bool wildcard = cur_subtop == "+" || cur_subtop == "#";
    const int next_first_wildcard_level_depth = first_wildcard_depth < 0 && wildcard ? level_depth : first_wildcard_depth;

    if (!wildcard)
    {
        auto multi_level = children.find("#");
        if (multi_level != children.cend())
        {
            collectClientsEndpoint(multi_level->second.get(), next_hash_of_tail, collected_clients, originating_fmq_client_group_id, level_depth + 1, next_first_wildcard_level_depth);
        }
    }

    const auto &sub_node = children.find(cur_subtop);

    if (sub_node != children.end())
    {
        collectClients(next_subtopic, end, sub_node->second.get(), next_hash_of_tail, collected_clients, originating_fmq_client_group_id, level_depth, next_first_wildcard_level_depth);
    }
}

AddSubscriptionResult LazySubscriptions::expandLazySubscriptions(
    TrackedSubscriptionMutationTask task, const std::shared_ptr<Session> &originating_session, const SubAckReleaseTrigger *suback_release_trigger,
    const std::vector<std::string> &subtopics, const uint8_t qos)
{
    assert(subtopics.size() == 0 || subtopics.at(0) != "$share");

    AddSubscriptionResult result;

    // Don't match the bridge's own internal subscriptions to ourself (the 'publish' lines in the config).
    if (originating_session->getClientType() == ClientType::LocalBridge)
        return result;

    std::vector<ReceivingLazySubscriber> collected_receivers;

    {
        auto data = root.shared_lock();

        if ((*data)->children.empty())
            return result;

        collectClients(subtopics.begin(), subtopics.end(), data->get(), 0, collected_receivers, originating_session->getFmqClientGroupId(), -1, -1);
    }

    if (collected_receivers.empty())
        return result;

    for(ReceivingLazySubscriber &receiver : collected_receivers)
    {
        assert(receiver.thread.lock());

        std::string pattern = receiver.getPattern(subtopics);
        uint8_t effective_qos = std::min(receiver.qos, qos);

        std::shared_ptr<BridgeState> bridgeState = receiver.receiver.lock();

        if (!bridgeState)
            continue;

        auto &tracked_subs = bridgeState->getTrackedSubscriptions();

        if (!tracked_subs)
            continue;

        TrackedSubscriptionMutation mutation(pattern, effective_qos, originating_session->getClientId(), originating_session, suback_release_trigger, task);
        tracked_subs->addTrackedSubscriptionMutation(std::move(mutation));

        result.expanded_count++;

        if (!result.affected_threads_lazy_subs)
            result.affected_threads_lazy_subs.emplace();

        result.affected_threads_lazy_subs.value().insert(receiver.thread.lock());
    }

    return result;
}

void registerLazySubscriptions(std::shared_ptr<BridgeState> &bridgeState)
{
    if (!bridgeState)
        return;

    if (!globals->lazySubscriptions)
        globals->lazySubscriptions.emplace();

    for (const BridgeLazySubscription &lazy_sub : bridgeState->c.lazySubscriptions)
    {
        globals->lazySubscriptions.value().addSubscription(bridgeState, lazy_sub.pattern, lazy_sub.qos, bridgeState->c.getFmqClientGroupId().value());
    }
}


