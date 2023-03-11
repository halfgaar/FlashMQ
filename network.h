/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2023 Wiebe Cazemier

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

#ifndef NETWORK_H
#define NETWORK_H

#include <arpa/inet.h>
#include "string"
#include <memory>

class Network
{
    sockaddr_in6 data;

    uint32_t in_mask = 0;

    uint32_t in6_mask[4];
    struct in6_addr network_addr_relevant_bits;

public:
    Network(const std::string &network);
    bool match(const struct sockaddr *addr) const ;
    bool match(const struct sockaddr_in *addr) const ;
    bool match(const struct sockaddr_in6 *addr) const;
};

#endif // NETWORK_H
