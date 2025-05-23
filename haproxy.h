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

struct proxy_hdr_v2
{
    uint8_t sig[12];  /* hex 0D 0A 0D 0A 00 0D 0A 51 55 49 54 0A */
    uint8_t ver_cmd;  /* protocol version and command */
    uint8_t fam;      /* protocol family and address */
    uint16_t len;     /* number of following bytes part of the header */
};

/* for TCP/UDP over IPv4, len = 12 */
struct proxy_ipv4_addr
{
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;
};

/* for TCP/UDP over IPv6, len = 36 */
struct proxy_ipv6_addr
{
    uint8_t  src_addr[16];
    uint8_t  dst_addr[16];
    uint16_t src_port;
    uint16_t dst_port;
};

/* for AF_UNIX sockets, len = 216 */
struct proxy_unix_addr
{
    uint8_t src_addr[108];
    uint8_t dst_addr[108];
};

union proxy_addr
{
    struct proxy_ipv4_addr ipv4_addr;
    struct proxy_ipv6_addr ipv6_addr;
    struct proxy_unix_addr proxy_unix_addr;
};

size_t read_ha_proxy_helper(int fd, void *buf, size_t nbytes);

enum class HaProxyConnectionType
{
    Remote,
    Local
};

#endif // HAPROXY_H
