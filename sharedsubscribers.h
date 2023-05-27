/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef SHAREDSUBSCRIBERS_H
#define SHAREDSUBSCRIBERS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "forward_declarations.h"
#include "subscription.h"

class SharedSubscribers
{
#ifdef TESTING
    friend class MainTests;
#endif
    std::vector<Subscription> members;
    std::unordered_map<std::string, int> index;
    int roundRobinCounter = 0;
    std::string shareName;

public:
    SharedSubscribers() noexcept;

    void setName(const std::string &name);
    Subscription& operator[](const std::string &clientid);
    const Subscription *getNext();
    const Subscription *getNext(size_t hash) const;
    void erase(const std::string &clientid);
    void purgeAndReIndex();
    bool empty() const;
    void getForSerializing(const std::string &topic, std::unordered_map<std::string, std::list<SubscriptionForSerializing>> &outputList) const;
};

#endif // SHAREDSUBSCRIBERS_H
