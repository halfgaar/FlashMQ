/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef NETWORK_H
#define NETWORK_H

#include <arpa/inet.h>
#include <string>
#include <memory>
#include <array>

class Network
{
    std::array<char, sizeof(struct sockaddr_storage)> data;
    sa_family_t family = AF_UNSPEC;

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
