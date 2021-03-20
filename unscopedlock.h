/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef UNSCOPEDLOCK_H
#define UNSCOPEDLOCK_H

#include <mutex>

/**
 * @brief The UnscopedLock class is a simple variety of the std::lock_guard or std::scoped_lock that allows optional locking using RAII.
 *
 * STL doesn't provide a similar feature, or am I missing something? You could do it with smart pointers, but I want to avoid having to
 * use the free store.
 */
class UnscopedLock
{
    std::mutex &managedMutex;
    bool locked = false;

public:
    ~UnscopedLock();

    UnscopedLock(std::mutex &mutex);
    void lock();
};

#endif // UNSCOPEDLOCK_H
