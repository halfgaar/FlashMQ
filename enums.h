/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef ENUMS_H
#define ENUMS_H

enum class X509ClientVerification
{
    None,
    X509IsEnough,
    X509AndUsernamePassword
};

enum class AllowListenerAnonymous
{
    None,
    Yes,
    No
};

enum class TLSVersion
{
    TLSv1_0,
    TLSv1_1,
    TLSv1_2,
    TLSv1_3
};

enum class OverloadMode
{
    Log,
    CloseNewClients
};

enum class ConnectionProtocol
{
    AcmeOnly,
    Mqtt,
    WebsocketMqtt
};

enum class Mqtt3QoSExceedAction
{
    Disconnect,
    Drop
};

#endif // ENUMS_H
