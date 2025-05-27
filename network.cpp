/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "network.h"
#include "utils.h"
#include <stdexcept>
#include <cstring>

Network::Network(const std::string &network)
{
    std::fill(this->data.begin(), this->data.end(), 0);

    if (strContains(network, "."))
    {
        struct sockaddr_in tmpaddr;
        std::memset(&tmpaddr, 0, sizeof(tmpaddr));

        tmpaddr.sin_family = AF_INET;

        int maskbits = inet_net_pton(AF_INET, network.c_str(), &tmpaddr.sin_addr, sizeof(struct in_addr));

        if (maskbits < 0)
            throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));

        std::memcpy(this->data.data(), &tmpaddr, sizeof(tmpaddr));

        uint32_t _netmask = (uint64_t)0xFFFFFFFFu << (32 - maskbits);
        this->in_mask = htonl(_netmask);

        this->family = AF_INET;
    }
    else if (strContains(network, ":"))
    {
        // Why does inet_net_pton not support AF_INET6...?

        struct sockaddr_in6 tmpaddr;
        std::memset(&tmpaddr, 0, sizeof(tmpaddr));

        tmpaddr.sin6_family = AF_INET6;

        std::vector<std::string> parts = splitToVector(network, '/', 2, false);
        std::string &addrPart = parts[0];
        int maskbits = 128;

        if (parts.size() == 2)
        {
            const std::string &maskstring = parts[1];

            const bool invalid_chars = std::any_of(maskstring.begin(), maskstring.end(), [](const char &c) { return c < '0' || c > '9'; });
            if (invalid_chars || maskstring.length() > 3)
                throw std::runtime_error(formatString("Mask '%s' is not valid", maskstring.c_str()));

            maskbits = std::stoi(maskstring);
        }

        if (inet_pton(AF_INET6, addrPart.c_str(), &tmpaddr.sin6_addr) != 1)
        {
            throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));
        }

        std::memcpy(this->data.data(), &tmpaddr, sizeof(tmpaddr));

        if (maskbits > 128 || maskbits < 0)
        {
            throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));
        }

        int m = maskbits;
        memset(in6_mask, 0, 16);
        int i = 0;
        const uint64_t x = 0xFFFFFFFF00000000;

        while (m >= 0)
        {
            int shift_remainder = std::min<int>(m, 32);
            uint32_t b = x >> shift_remainder;
            in6_mask[i++] = htonl(b);
            m -= 32;
        }

        for (int i = 0; i < 4; i++)
        {
            network_addr_relevant_bits.__in6_u.__u6_addr32[i] = tmpaddr.sin6_addr.__in6_u.__u6_addr32[i] & in6_mask[i];
        }

        this->family = AF_INET6;
    }
    else
    {
        throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));
    }
}

bool Network::match(const sockaddr *addr) const
{
    const sa_family_t fam_arg = getFamilyFromSockAddr(addr);

    if (this->family != fam_arg)
        return false;

    if (this->family == AF_INET)
    {
        struct sockaddr_in tmp_this;
        struct sockaddr_in tmp_arg;

        std::memcpy(&tmp_this, this->data.data(), sizeof(tmp_this));
        std::memcpy(&tmp_arg, addr, sizeof(tmp_arg));

        return (tmp_this.sin_addr.s_addr & this->in_mask) == (tmp_arg.sin_addr.s_addr & this->in_mask);
    }
    else if (this->family == AF_INET6)
    {
        struct sockaddr_in6 tmp_arg;
        std::memcpy(&tmp_arg, addr, sizeof(tmp_arg));

        struct in6_addr arg_addr_relevant_bits;
        for (int i = 0; i < 4; i++)
        {
            arg_addr_relevant_bits.__in6_u.__u6_addr32[i] = tmp_arg.sin6_addr.__in6_u.__u6_addr32[i] & in6_mask[i];
        }

        uint8_t matches[4];
        for (int i = 0; i < 4; i++)
        {
            matches[i] = arg_addr_relevant_bits.__in6_u.__u6_addr32[i] == network_addr_relevant_bits.__in6_u.__u6_addr32[i];
        }

        return (matches[0] & matches[1] & matches[2] & matches[3]);
    }

    return false;
}

bool Network::match(const sockaddr_in *addr) const
{
    const struct sockaddr *_addr = reinterpret_cast<const struct sockaddr*>(addr);
    return match(_addr);
}

bool Network::match(const sockaddr_in6 *addr) const
{
    const struct sockaddr *_addr = reinterpret_cast<const struct sockaddr*>(addr);
    return match(_addr);
}
