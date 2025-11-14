/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef RETAINEDMESSAGE_H
#define RETAINEDMESSAGE_H

#include <string>
#include "types.h"

struct RetainedMessage
{
    Publish publish;

    RetainedMessage(const Publish &publish);
    RetainedMessage(const RetainedMessage&) = default;
    RetainedMessage &operator=(const RetainedMessage&) = delete;
    RetainedMessage &operator=(RetainedMessage&&) = delete;

    bool operator==(const RetainedMessage &rhs) const;
    RetainedMessage &operator=(const Publish &pub);
    void setRetainData();
    bool empty() const;
    uint32_t getSize() const;
    bool hasExpired() const;
};

namespace std {

    template <>
    struct hash<RetainedMessage>
    {
        std::size_t operator()(const RetainedMessage& k) const
        {
            using std::size_t;
            using std::hash;
            using std::string;

            return hash<string>()(k.publish.topic);
        }
    };

}

#endif // RETAINEDMESSAGE_H
