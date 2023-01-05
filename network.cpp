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

#include "network.h"
#include "utils.h"
#include <stdexcept>

Network::Network(const std::string &network)
{
    memset(&this->data, 0, sizeof (struct sockaddr_in6));

    if (strContains(network, "."))
    {
        struct sockaddr_in *_sockaddr_in = reinterpret_cast<struct sockaddr_in*>(&this->data);

        _sockaddr_in->sin_family = AF_INET;

        int maskbits = inet_net_pton(AF_INET, network.c_str(), &_sockaddr_in->sin_addr, sizeof(struct in_addr));

        if (maskbits < 0)
            throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));

        uint32_t _netmask = (uint64_t)0xFFFFFFFFu << (32 - maskbits);
        this->in_mask = htonl(_netmask);
    }
    else if (strContains(network, ":"))
    {
        // Why does inet_net_pton not support AF_INET6...?

        struct sockaddr_in6 *_sockaddr_in6 = reinterpret_cast<struct sockaddr_in6*>(&this->data);

        _sockaddr_in6->sin6_family = AF_INET6;

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

        if (inet_pton(AF_INET6, addrPart.c_str(), &_sockaddr_in6->sin6_addr) != 1)
        {
            throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));
        }

        if (maskbits > 128 || maskbits < 0)
        {
            throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));
        }

        this->in6_zero = _mm_set1_epi8(0);
        this->in6_network_128 = _mm_loadu_si128((__m128i*)&_sockaddr_in6->sin6_addr);

        int m = maskbits;
        uint32_t mask[4];
        memset(mask, 0, 16);
        int i = 0;
        const uint64_t x = 0xFFFFFFFF00000000;

        while (m >= 0)
        {
            int shift_remainder = std::min<int>(m, 32);
            uint32_t b = x >> shift_remainder;
            mask[i++] = htonl(b);
            m -= 32;
        }

        this->in6_mask = _mm_loadu_si128((__m128i*)mask);
    }
    else
    {
        throw std::runtime_error(formatString("Network '%s' is not a valid network notation.", network.c_str()));
    }
}

bool Network::match(const sockaddr *addr) const
{
    const struct sockaddr* _sockaddr = reinterpret_cast<const struct sockaddr*>(&this->data);

    if (_sockaddr->sa_family == AF_INET)
    {
        const struct sockaddr_in *_sockaddr_in = reinterpret_cast<const struct sockaddr_in*>(&this->data);
        const struct sockaddr_in *_addr = reinterpret_cast<const struct sockaddr_in*>(addr);
        return _sockaddr->sa_family == addr->sa_family && ((_sockaddr_in->sin_addr.s_addr & this->in_mask) == (_addr->sin_addr.s_addr & this->in_mask));
    }
    else if (_sockaddr->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 *_addr = reinterpret_cast<const struct sockaddr_in6*>(addr);

        __m128i _addr_128 = _mm_loadu_si128((__m128i*)&_addr->sin6_addr);
        __m128i _xored_128 = _mm_xor_si128(this->in6_network_128, _addr_128);
        _xored_128 = _mm_and_si128(this->in6_mask, _xored_128);
        __m128i equals_to_zero = _mm_cmpeq_epi8(_xored_128, this->in6_zero);
        int equals_to_zero_mask = _mm_movemask_epi8(equals_to_zero);
        return equals_to_zero_mask == 0xFFFF;
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
