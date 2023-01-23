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
