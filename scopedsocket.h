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

#ifndef SCOPEDSOCKET_H
#define SCOPEDSOCKET_H

#include <fcntl.h>
#include <unistd.h>

/**
 * @brief The ScopedSocket struct allows for a bit of RAII and move semantics on a socket fd.
 */
struct ScopedSocket
{
    int socket = 0;
    ScopedSocket(int socket);
    ScopedSocket(ScopedSocket &&other);
    ~ScopedSocket();
};

#endif // SCOPEDSOCKET_H
