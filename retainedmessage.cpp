/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "retainedmessage.h"
#include "threadglobals.h"
#include "settings.h"

RetainedMessage::RetainedMessage(const Publish &publish) :
    publish(publish)
{
    setRetainData();
}

bool RetainedMessage::operator==(const RetainedMessage &rhs) const
{
    return this->publish.topic == rhs.publish.topic;
}

void RetainedMessage::setRetainData()
{
    this->publish.retain = true;
    const Settings *settings = ThreadGlobals::getSettings();
    this->publish.setExpireAfterToCeiling(settings->expireRetainedMessagesAfterSeconds);
}

RetainedMessage &RetainedMessage::operator=(const Publish &pub)
{
    this->publish = pub;
    setRetainData();
    return *this;
}

bool RetainedMessage::empty() const
{
    return publish.payload.empty();
}

uint32_t RetainedMessage::getSize() const
{
    return publish.topic.length() + publish.payload.length() + 1;
}

/**
 * @brief RetainedMessage::hasExpired is more dynamic than a publish's expire info, because the settings may have changed in the mean time.
 * @return
 */
bool RetainedMessage::hasExpired() const
{
    if (this->publish.hasExpired())
        return true;

    const Settings *settings = ThreadGlobals::getSettings();
    std::chrono::milliseconds expireAge(settings->expireRetainedMessagesAfterSeconds);

    if (this->publish.getAge<std::chrono::milliseconds>() > expireAge)
        return true;

    return false;

}
