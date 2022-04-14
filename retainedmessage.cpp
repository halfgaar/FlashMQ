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

#include "retainedmessage.h"

RetainedMessage::RetainedMessage(const Publish &publish) :
    publish(publish)
{
    this->publish.retain = true;
    this->publish.splitTopic = false;
}

bool RetainedMessage::operator==(const RetainedMessage &rhs) const
{
    return this->publish.topic == rhs.publish.topic;
}

bool RetainedMessage::empty() const
{
    return publish.payload.empty();
}

uint32_t RetainedMessage::getSize() const
{
    return publish.topic.length() + publish.payload.length() + 1;
}
