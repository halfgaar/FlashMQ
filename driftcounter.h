#ifndef DRIFTCOUNTER_H
#define DRIFTCOUNTER_H

#include <chrono>
#include <array>

/**
 * @brief The DriftCounter class allows measuring drift in threads.
 *
 * There is no thread syncing / mutexes. Values can be retrieved from other threads, but some values may
 * not be super accurate, which is fine.
 */
class DriftCounter
{
    std::chrono::time_point<std::chrono::steady_clock> last_update = std::chrono::steady_clock::now();
    std::chrono::milliseconds last_drift;
    std::array<std::chrono::milliseconds, 16> many_drifts;
    unsigned int many_index = 0;

public:
    DriftCounter();
    void update(std::chrono::time_point<std::chrono::steady_clock> call_time);
    std::chrono::milliseconds getDrift() const;
    std::chrono::milliseconds getAvgDrift() const;
};

#endif // DRIFTCOUNTER_H
