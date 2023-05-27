/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef HAPROXY_H
#define HAPROXY_H

#include <sys/socket.h>
#include <stdint.h>
#include <unistd.h>

union proxy_addr
{
    /* for TCP/UDP over IPv4, len = 12 */
    struct
    {
        uint32_t src_addr;
        uint32_t dst_addr;
        uint16_t src_port;
        uint16_t dst_port;
    } ipv4_addr;

    /* for TCP/UDP over IPv6, len = 36 */
    struct
    {
         uint8_t  src_addr[16];
         uint8_t  dst_addr[16];
         uint16_t src_port;
         uint16_t dst_port;
    } ipv6_addr;

    /* for AF_UNIX sockets, len = 216 */
    struct
    {
         uint8_t src_addr[108];
         uint8_t dst_addr[108];
    } unix_addr;
};

size_t read_ha_proxy_helper(int fd, void *buf, size_t nbytes);

enum class HaProxyConnectionType
{
    Remote,
    Local
};

#endif // HAPROXY_H
