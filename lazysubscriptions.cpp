#include <utility>
#include "lazysubscriptions.h"
#include "threaddata.h"
#include "globals.h"

LazySubscriber::LazySubscriber(
        const std::shared_ptr<BridgeState> &bridgeState, const std::shared_ptr<ThreadData> &thread,
        int min_required_wildcard_depth, uint8_t qos, const std::string &share_name) :
    bridge(bridgeState),
    thread(thread),
    min_required_wildcard_depth(min_required_wildcard_depth),
    qos(qos),
    share_name(share_name)
{

}


ReceivingLazySubscriber::ReceivingLazySubscriber(
        const std::weak_ptr<BridgeState> &receiver, const std::shared_ptr<ThreadData> &thread, uint8_t qos, const std::string &share_name,
        size_t depth) :
    receiver(receiver),
    thread(thread),
    qos(qos),
    share_name(share_name),
    depth(depth)
{

}

std::string ReceivingLazySubscriber::getPatternWithShareName(const std::vector<std::string> &subtopics) const
{
    std::string pattern;

    if (!share_name.empty())
    {
        pattern.append(std::string_view("$share/"));
        pattern.append(share_name);
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
    uint8_t qos, const std::string &share_name)
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
    auto subscribers = node->subscribers.unique_lock();
    assert(!bridgeState->threadData.expired());
    subscribers->emplace_back(bridgeState, bridgeState->threadData.lock(), min_required_wildcard_depth, qos, share_name);
    bridgeState->constructTrackedSubscriptions();
}

void LazySubscriptions::collectClientsEndpoint(
    LazySubscriptionNode *this_node, std::vector<ReceivingLazySubscriber> &collected_clients, const int level_depth, int first_wildcard_depth)
{
    auto subscriber_data = this_node->subscribers.shared_lock();

    for (const LazySubscriber &s : *subscriber_data)
    {
        if (first_wildcard_depth > -1 && first_wildcard_depth < s.min_required_wildcard_depth)
            continue;

        collected_clients.emplace_back(s.bridge, s.thread, s.qos, s.share_name, level_depth);

        if (collected_clients.back().receiver.expired())
        {
            collected_clients.pop_back();
        }
    }
}

void LazySubscriptions::collectClients(
    std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
    LazySubscriptionNode *this_node, std::vector<ReceivingLazySubscriber> &collected_clients, int level_depth, int first_wildcard_depth)
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
    const auto next_subtopic = ++cur_subtopic_it;
    const bool wildcard = cur_subtop == "+" || cur_subtop == "#";
    const int next_first_wildcard_level_depth = first_wildcard_depth < 0 && wildcard ? level_depth : first_wildcard_depth;

    if (!wildcard)
    {
        auto multi_level = children.find("#");
        if (multi_level != children.cend())
        {
            collectClientsEndpoint(multi_level->second.get(), collected_clients, level_depth + 1, next_first_wildcard_level_depth);
        }
    }

    const auto &sub_node = children.find(cur_subtop);

    if (sub_node != children.end())
    {
        collectClients(next_subtopic, end, sub_node->second.get(), collected_clients, level_depth, next_first_wildcard_level_depth);
    }
}

void LazySubscriptions::expandLazySubscriptions(
    TrackedSubscriptionMutationTask task, const std::shared_ptr<Session> originating_session,
    const std::vector<std::string> &subtopics, const uint8_t qos)
{
    assert(subtopics.size() == 0 || subtopics.at(0) != "$share");

    // Don't match the bridge's own internal subscriptions to ourself (the 'publish' lines in the config).
    if (originating_session->getClientType() == ClientType::LocalBridge)
        return;

    std::vector<ReceivingLazySubscriber> collected_receivers;

    {
        auto data = root.shared_lock();

        if ((*data)->children.empty())
            return;

        collectClients(subtopics.begin(), subtopics.end(), data->get(), collected_receivers, -1, -1);
    }

    if (collected_receivers.empty())
        return;

    for(ReceivingLazySubscriber &receiver : collected_receivers)
    {
        assert(receiver.thread);

        std::string pattern = receiver.getPatternWithShareName(subtopics);
        uint8_t effective_qos = std::min(receiver.qos, qos);

        std::shared_ptr<BridgeState> bridgeState = receiver.receiver.lock();

        if (!bridgeState)
            continue;

        auto &tracked_subs = bridgeState->getTrackedSubscriptions();

        if (!tracked_subs)
            continue;

        const bool doWakeup = tracked_subs->addTrackedSubscriptionMutation(
            TrackedSubscriptionMutation(pattern, effective_qos, originating_session->getClientId(), originating_session, task));

        if (doWakeup)
        {
            receiver.thread->queueProcessTrackedSubscriptionMutations(bridgeState, ProcessTrackedSubscriptionMutationsModifier::FirstFinishResending);
        }
    }
}

void registerLazySubscriptions(std::shared_ptr<BridgeState> &bridgeState)
{
    if (!bridgeState)
        return;

    if (!globals->lazySubscriptions)
        globals->lazySubscriptions.emplace();

    for (const BridgeLazySubscription &lazy_sub : bridgeState->c.lazySubscriptions)
    {
        globals->lazySubscriptions.value().addSubscription(bridgeState, lazy_sub.pattern, lazy_sub.qos, lazy_sub.share_name);
    }
}


