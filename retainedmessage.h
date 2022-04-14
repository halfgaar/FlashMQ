/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

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

#ifndef RETAINEDMESSAGE_H
#define RETAINEDMESSAGE_H

#include <string>
#include "types.h"

struct RetainedMessage
{
    Publish publish;

    RetainedMessage(const Publish &publish);

    bool operator==(const RetainedMessage &rhs) const;
    bool empty() const;
    uint32_t getSize() const;
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
