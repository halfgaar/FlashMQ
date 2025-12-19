#include <optional>
#include "globals.h"
#include "threadglobals.h"

Globals globals;

/**
 * Normally there's 'ThreadGlobals::getThreadData()', but there are places where we can be called
 * from non-worker-threads, like plugin custom threads. This gives us the worker's thread one, or
 * a deterministic other one. Determinism is important to ensure order, when publishing for instance.
 *
 * Note that there is overhead here, so use only when required.
 */
CheckedSharedPtr<ThreadData> Globals::GlobalsData::getDeterministicThreadData()
{
    CheckedSharedPtr<ThreadData> result = ThreadGlobals::getThreadData();

    if (result)
        return result;

    static thread_local std::shared_ptr<ThreadData> thread_data_copy;

    if (thread_data_copy)
        return thread_data_copy;

    static std::atomic<size_t> index {0};
    auto locked = threadDatas.lock();
    thread_data_copy = locked->at(index++ % locked->size());
    return thread_data_copy;
}
