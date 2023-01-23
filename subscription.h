#ifndef SUBSCRIPTION_H
#define SUBSCRIPTION_H

#include <memory>
#include "session.h"

struct Subscription
{
    std::weak_ptr<Session> session; // Weak pointer expires when session has been cleaned by 'clean session' connect or when it was remove because it expired
    uint8_t qos;
    bool operator==(const Subscription &rhs) const;
    void reset();
};

#endif // SUBSCRIPTION_H
