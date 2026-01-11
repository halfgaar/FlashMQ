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
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>

#define PP2_TYPE_ALPN           0x01
#define PP2_TYPE_AUTHORITY      0x02
#define PP2_TYPE_CRC32C         0x03
#define PP2_TYPE_NOOP           0x04
#define PP2_TYPE_UNIQUE_ID      0x05
#define PP2_TYPE_SSL            0x20
#define PP2_SUBTYPE_SSL_VERSION 0x21
#define PP2_SUBTYPE_SSL_CN      0x22
#define PP2_SUBTYPE_SSL_CIPHER  0x23
#define PP2_SUBTYPE_SSL_SIG_ALG 0x24
#define PP2_SUBTYPE_SSL_KEY_ALG 0x25
#define PP2_TYPE_NETNS          0x30

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

struct HaProxySslData
{
    uint8_t client = 0;
    uint32_t verify = 0;
    std::optional<std::string> ssl_version;
    std::optional<std::string> ssl_cn;
    std::optional<std::string> ssl_cipher;
    std::optional<std::string> ssl_sig_alg;
    std::optional<std::string> ssl_key_alg;
};

size_t read_ha_proxy_helper(int fd, void *buf, size_t nbytes);
std::unordered_map<int, std::variant<std::string, HaProxySslData>> read_ha_proxy_pp2_tlv(const std::vector<unsigned char> &data, int &recurse_counter);

enum class HaProxyConnectionType
{
    None,
    Remote,
    Local
};

#endif // HAPROXY_H
