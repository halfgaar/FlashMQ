/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "sharedsubscribers.h"
#include <cassert>

SharedSubscribers::SharedSubscribers() noexcept
{

}

void SharedSubscribers::setName(const std::string &name)
{
    if (!shareName.empty() || name.empty())
        return;

    this->shareName = name;
}

/**
 * @brief SharedSubscribers::operator [] access or create a shared subscription in a shared subscription.
 * @param clientid
 * @return
 *
 * Note that the reference returned will likely be invalidated when you call it again, so don't keep lingering references around.
 */
Subscription &SharedSubscribers::operator[](const std::string &clientid)
{
    auto index_pos = index.find(clientid);
    if (index_pos != index.end())
    {
        const int index = index_pos->second;
        assert(index < static_cast<int>(members.size()));
        return members[index];
    }

    const int newIndex = members.size();
    index[clientid] = newIndex;
    members.emplace_back();
    Subscription &r = members.back();
    return r;
}

const Subscription *SharedSubscribers::getNext()
{
    const Subscription *result = nullptr;

    for (size_t i = 0; i < members.size(); i++)
    {
        // This counter use is not thread safe / atomic, but it doesn't matter much.
        const Subscription &s = members[roundRobinCounter++ % members.size()];

        if (!s.session.expired())
        {
            result = &s;
            break;
        }
    }

    return result;
}

const Subscription *SharedSubscribers::getNext(size_t hash) const
{
    const Subscription *result = nullptr;

    size_t pos = hash % members.size();

    for (size_t i = 0; i < members.size(); i++)
    {
        const Subscription &s = members[pos++ % members.size()];

        if (!s.session.expired())
        {
            result = &s;
            break;
        }
    }

    return result;
}

void SharedSubscribers::erase(const std::string &clientid)
{
    auto index_pos = index.find(clientid);
    if (index_pos != index.end())
    {
        const int index = index_pos->second;
        assert(index < static_cast<int>(members.size()));
        members[index].reset();
    }
}

void SharedSubscribers::purgeAndReIndex()
{
    int i = 0;
    std::vector<Subscription> newMembers;
    std::unordered_map<std::string, int> newIndex;

    for (auto &pair : index)
    {
        const int index = pair.second;
        Subscription &sub = members[index];

        if (sub.session.expired())
            continue;

        newMembers.push_back(sub);
        newIndex[pair.first] = i;
        i++;
    }

    this->members = std::move(newMembers);
    this->index = std::move(newIndex);
}

bool SharedSubscribers::empty() const
{
    return members.empty();
}

void SharedSubscribers::getForSerializing(const std::string &topic, std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &outputList) const
{
    for (const Subscription &member : members)
    {
        std::shared_ptr<Session> ses = member.session.lock();
        if (ses)
        {
            SubscriptionForSerializing sub(ses->getClientId(), member.qos, member.noLocal, this->shareName);
            outputList[topic].push_back(sub);
        }
    }
}
