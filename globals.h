#ifndef GLOBALS_H
#define GLOBALS_H

#include <memory>

#include "subscriptionstore.h"
#include "globalstats.h"

/**
 * The idea about it being a shared pointer is having globals that are still tied to a MainApp instance (which
 * should assign a new global object upon creation and destruction). This is mainly for keeping the memory model
 * between normal FlashMQ and the re-instantiated MainApps in the test program the same, which wouldn't be the
 * case by when having static variables for globals.
 */
class Globals
{
    struct GlobalsData
    {
        bool quitting = false;
        std::shared_ptr<SubscriptionStore> subscriptionStore = std::make_shared<SubscriptionStore>();
        GlobalStats stats;
    };

    std::shared_ptr<GlobalsData> data = std::make_shared<GlobalsData>();
public:

    GlobalsData *operator->() const
    {
        return data.get();
    }
};

extern Globals globals;

#endif // GLOBALS_H
