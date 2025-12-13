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

    static thread_local std::optional<size_t> thread_index;

    if (!thread_index)
    {
        static size_t index = 0;
        static std::mutex m;
        std::lock_guard locker(m);
        thread_index = index++;
    }

    auto locked = threadDatas.lock();
    return locked->at(thread_index.value() % locked->size());
}
