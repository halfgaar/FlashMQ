#include "driftcounter.h"

#include <algorithm>
#include <numeric>

#include "settings.h"

DriftCounter::DriftCounter()
{

}

void DriftCounter::update(std::chrono::time_point<std::chrono::steady_clock> call_time)
{
    auto now = std::chrono::steady_clock::now();
    std::chrono::milliseconds diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - call_time);
    diff = std::max<std::chrono::milliseconds>(diff, std::chrono::milliseconds(0));
    many_drifts.at(many_index++ & (many_drifts.size() - 1)) = diff;

    last_update = now;
    last_drift = diff;
}

std::chrono::milliseconds DriftCounter::getDrift() const
{
    auto now = std::chrono::steady_clock::now();

    const auto last_update_copy = last_update;

    if (last_update_copy + (std::chrono::milliseconds(HEARTBEAT_INTERVAL) * 2 ) < now)
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update_copy);

    return last_drift;
}

/**
 * @brief DriftCounter::getAvgDrift returns the averate of the last x updates. It's not thread safe, so you may
 * average updates from different calls to update().
 * @return
 */
std::chrono::milliseconds DriftCounter::getAvgDrift() const
{
    const double total = std::accumulate(many_drifts.begin(), many_drifts.end(), std::chrono::milliseconds(0)).count();
    std::chrono::milliseconds avg = std::chrono::milliseconds(static_cast<int>(total / static_cast<double>(many_drifts.size())));
    return avg;
}
