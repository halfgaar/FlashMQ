/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "haproxy.h"

#include <errno.h>
#include <stdexcept>
#include "utils.h"
#include "exceptions.h"

/**
 * @brief read_ha_proxy_helper This function simplyfies reading for HAProxy in that it doesn't need to deal with incomplete data (because HAProxy frames
 * are always complete), so those cases can just be an error.
 * @param fd
 * @param buf
 * @param nbytes
 * @return
 */
size_t read_ha_proxy_helper(int fd, void *buf, size_t nbytes)
{
    if (nbytes == 0)
        return 0;

    ssize_t n;
    while ((n = read(fd, buf, nbytes)) != 0)
    {
        if (n == 0)
            throw BadClientException("Client disconnected before all HA proxy data could be read");
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                throw BadClientException("Incomplete HAProxy data");
            else
                check<BadClientException>(n);
        }

        break;
    }

    if (static_cast<ssize_t>(nbytes) != n)
        throw BadClientException("Not an HAProxy frame.");

    return n;
}

std::optional<std::string> get_ha_proxy_pp2_field(const std::unordered_map<int, std::variant<std::string, HaProxySslData>> &fields, int key)
{
    auto pos = fields.find(key);

    if (pos == fields.end())
        return {};

    return std::get<std::string>(pos->second);
}

HaProxySslData read_ha_proxy_pp2_ssl(const std::vector<unsigned char> &data, int &recurse_counter)
{
    HaProxySslData result;
    size_t pos = 0;

    result.client = data.at(pos++);

    for (int shift = 24; shift >= 0; shift -= 8)
    {
        result.verify = (data.at(pos++) << shift);
    }

    auto sub_vec = make_vector<unsigned char>(data, pos, data.size() - pos);
    const std::unordered_map<int, std::variant<std::string, HaProxySslData>> fields = read_ha_proxy_pp2_tlv(sub_vec, recurse_counter);

    result.ssl_version = get_ha_proxy_pp2_field(fields, PP2_SUBTYPE_SSL_VERSION);
    result.ssl_cn = get_ha_proxy_pp2_field(fields, PP2_SUBTYPE_SSL_CN);

    if (result.ssl_version)
    {
        if (!isValidUtf8Generic(result.ssl_version.value()))
            throw BadClientException("HAProxy pp2 ssl text fields are not valid UTF8.");
    }

    if (result.ssl_cn)
    {
        if (!isValidUtf8Generic(result.ssl_cn.value()))
            throw BadClientException("HAProxy pp2 ssl text fields are not valid UTF8.");
    }

    // We don't use this fields (yet), so ignorning for now. Here for future reference.
    //result.ssl_cipher = get_ha_proxy_pp2_field(fields, PP2_SUBTYPE_SSL_CIPHER);
    //result.ssl_sig_alg = get_ha_proxy_pp2_field(fields, PP2_SUBTYPE_SSL_SIG_ALG);
    //result.ssl_key_alg = get_ha_proxy_pp2_field(fields, PP2_SUBTYPE_SSL_KEY_ALG);

    return result;
}

/**
 * See info about Type-Length-Value (TLV vectors in https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
 *
 * Many of the fields aren't actually strings, but since we're only using SSL data right now, we'll just read those other
 * fields as raw data and not use them.
 */
std::unordered_map<int, std::variant<std::string, HaProxySslData>> read_ha_proxy_pp2_tlv(const std::vector<unsigned char> &data, int &recurse_counter)
{
    recurse_counter++;

    if (recurse_counter > 2)
        throw BadClientException("Invalid HAProxy TLV structure.");

    size_t pos = 0;
    std::unordered_map<int, std::variant<std::string, HaProxySslData>> result;

    while (pos < data.size())
    {
        const uint8_t type{data.at(pos++)};
        const uint8_t length_hi{data.at(pos++)};
        const uint8_t length_lo{data.at(pos++)};
        const size_t len{static_cast<size_t>((length_hi << 8) + length_lo)};

        if (pos + len > data.size())
            throw BadClientException("Client specifies invalid length in haproxy TLV");

        if (type == PP2_TYPE_SSL)
        {
            auto sub_vec = make_vector<unsigned char>(data, pos, len);

            const auto emplace_result = result.try_emplace(type, read_ha_proxy_pp2_ssl(sub_vec, recurse_counter));
            pos += len;

            if (!emplace_result.second)
            {
                std::ostringstream oss;
                oss << "Client specifies haproxy PP2 field 0x" << std::hex << type << " multiple times.";
                throw BadClientException(oss.str());
            }

            continue;
        }

        const auto emplace_result = result.try_emplace(type, make_string(data, pos, len));
        pos += len;

        if (!emplace_result.second)
        {
            std::ostringstream oss;
            oss << "Client specifies haproxy PP2 field 0x" << std::hex << type << " multiple times.";
            throw BadClientException(oss.str());
        }
    }

    if (pos != data.size())
        throw BadClientException("Client specifies invalid length in haproxy TLV");

    return result;
}






