/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef SUBSCRIPTION_H
#define SUBSCRIPTION_H

#include <memory>
#include "session.h"

struct Subscription
{
    std::weak_ptr<Session> session; // Weak pointer expires when session has been cleaned by 'clean session' connect or when it was remove because it expired
    uint8_t qos;
    bool noLocal = false;
    bool retainAsPublished = false;
    bool operator==(const Subscription &rhs) const;
    void reset();
};

#endif // SUBSCRIPTION_H
