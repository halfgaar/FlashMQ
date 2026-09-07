#ifndef GLOBALS_H
#define GLOBALS_H

#include <memory>
#include <pthread.h>

#include "subscriptionstore.h"
#include "lazysubscriptions.h"
#include "globalstats.h"
#include "bridgeconfig.h"
#include "checkedsharedptr.h"
#include "mutexowned.h"

/**
 * The idea about Globals being recreatable is having globals that are still tied to a MainApp instance (which
 * should assign a new global object upon creation and destruction). This is mainly for keeping the memory model
 * between normal FlashMQ and the re-instantiated MainApps in the test program the same, which wouldn't be the
 * case by when having static variables for globals.
 */
class Globals
{
    class GlobalsData
    {
        MutexOwned<CheckedSharedPtr<LazySubscriptions>> lazySubscriptions;

    public:
        bool quitting = false;
        pthread_t createdByThread = pthread_self();
        SubscriptionStore subscriptionStore;
        GlobalStats stats;
        BridgeClientGroupIds bridgeClientGroupIds;
        MutexOwned<std::vector<std::shared_ptr<ThreadData>>> threadDatas;

        CheckedSharedPtr<ThreadData> getDeterministicThreadData();
        CheckedSharedPtr<LazySubscriptions> getLazySubscriptions(bool construct);
        void destroyLazySubscriptions();

        GlobalsData() = default;
        GlobalsData(const GlobalsData&) = delete;
        GlobalsData &operator=(const GlobalsData&) = delete;
    };

    std::unique_ptr<GlobalsData> data = std::make_unique<GlobalsData>();
public:

    GlobalsData *operator->() const
    {
        return data.get();
    }
};

extern Globals globals;

#endif // GLOBALS_H
