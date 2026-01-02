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

    static thread_local std::weak_ptr<ThreadData> thread_data_copy_weak;

    std::shared_ptr<ThreadData> result2 = thread_data_copy_weak.lock();

    if (result2)
        return result2;

    static std::atomic<size_t> index {0};
    auto locked = threadDatas.lock();
    result2 = locked->at(index++ % locked->size());
    thread_data_copy_weak = result2;
    return result2;
}
