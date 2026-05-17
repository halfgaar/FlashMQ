/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2026 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "usedids.h"

#include <cassert>
#include <cstring>

UsedIds::UsedIds()
{

}

bool UsedIds::bits_is_used(const uint16_t id) const
{
    return m_bits.value().at(id / 64) & (1ULL << (id % 64));
}

void UsedIds::bits_mark_used(const uint16_t id)
{
    const auto bit_mask = 1ULL << (id % 64);
    auto &cur = m_bits.value().at(id / 64);
    if ((cur & bit_mask) == 0)
        m_bits_used++;
    cur |= bit_mask;
}

void UsedIds::bits_mark_free(const uint16_t id)
{
    const auto bit_mask = 1ULL << (id % 64);
    auto &cur = m_bits.value().at(id / 64);
    if (cur & bit_mask)
        m_bits_used--;
    cur &= ~(bit_mask);
}

bool UsedIds::ids_is_used(const uint16_t id) const
{
    for (size_t i = 0; i < m_ids_used; i++)
    {
        if (m_ids.at(i) == id)
            return true;
    }
    return false;
}

void UsedIds::ids_mark_used(const uint16_t id)
{
    if (ids_is_used(id))
        return;

    m_ids.at(m_ids_used++) = id;
}

void UsedIds::ids_mark_free(const uint16_t id)
{
    for (size_t i = 0; i < m_ids_used; i++)
    {
        if (m_ids.at(i) != id)
            continue;

        const auto pos = m_ids.data() + i;
        m_ids_used--;
        const size_t tail_len = m_ids_used - i;

        std::memmove(pos, pos + 1, tail_len * sizeof(id));
        return;
    }
}

bool UsedIds::contains(uint16_t id) const
{
    assert(id > 0);

    if (m_bits)
        return bits_is_used(id);

    return ids_is_used(id);
}

void UsedIds::insert(uint16_t id)
{
    assert(id > 0);

    if (!m_bits && m_ids_used >= SMALL_MAX)
    {
        m_bits.emplace(WORDS);

        for (size_t i = 0; i < m_ids_used; i++)
        {
            bits_mark_used(m_ids.at(i));
        }

        m_ids_used = 0;
    }

    if (m_bits)
        bits_mark_used(id);
    else
        ids_mark_used(id);
}

void UsedIds::erase(uint16_t id)
{
    assert(id > 0);

    if (m_bits)
    {
        bits_mark_free(id);

        if (m_bits_used == 0)
        {
            m_ids_used = 0;
            m_bits.reset();
        }
    }
    else
        ids_mark_free(id);
}
