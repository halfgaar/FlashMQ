/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "packetdatatypes.h"

#include "threadglobals.h"
#include "settings.h"

ConnectData::ConnectData()
{
    const Settings *settings = ThreadGlobals::getSettings();

    client_receive_max = settings->maxQosMsgPendingPerClient;
    session_expire = settings->getExpireSessionAfterSeconds();
    max_outgoing_packet_size = settings->maxPacketSize;
}

ConnAckData::ConnAckData()
{
    const Settings *settings = ThreadGlobals::getSettings();

    client_receive_max = settings->maxQosMsgPendingPerClient;
    session_expire = settings->getExpireSessionAfterSeconds();
    max_outgoing_packet_size = settings->maxPacketSize;
}
