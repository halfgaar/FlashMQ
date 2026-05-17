/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2026 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef USEDIDS_H
#define USEDIDS_H

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>
#include <array>

class UsedIds
{
    static constexpr size_t NUM_VALUES = 65536;
    static constexpr size_t WORDS = NUM_VALUES / 64;
    static constexpr size_t SMALL_MAX = 8;

    std::array<uint16_t, SMALL_MAX> m_ids {};
    size_t m_ids_used = 0;

    std::optional<std::vector<uint64_t>> m_bits;
    size_t m_bits_used = 0;

    bool bits_is_used(const uint16_t id) const;
    void bits_mark_used(const uint16_t id);
    void bits_mark_free(const uint16_t id);

    bool ids_is_used(const uint16_t id) const;
    void ids_mark_used(const uint16_t id);
    void ids_mark_free(const uint16_t id);

public:
    UsedIds();

    bool contains(const uint16_t id) const;
    void insert(const uint16_t id);
    void erase(const uint16_t id);
};

#endif // USEDIDS_H
