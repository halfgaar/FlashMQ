/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef LISTENER_H
#define LISTENER_H

#include <string>
#include <memory>

#include "sslctxmanager.h"
#include "enums.h"

enum class ListenerProtocol
{
    IPv46,
    IPv4,
    IPv6
};

struct Listener
{
    ListenerProtocol protocol = ListenerProtocol::IPv46;
    std::string inet4BindAddress;
    std::string inet6BindAddress;
    int port = 0;
    bool websocket = false;
    bool haproxy = false;
    std::string sslFullchain;
    std::string sslPrivkey;
    std::string clientVerificationCaFile;
    std::string clientVerificationCaDir;
    bool clientVerifictionStillDoAuthn = false;
    std::unique_ptr<SslCtxManager> sslctx;
    AllowListenerAnonymous allowAnonymous = AllowListenerAnonymous::None;

    void isValid();
    bool isSsl() const;
    bool isHaProxy() const;
    std::string getProtocolName() const;
    void loadCertAndKeyFromConfig();
    X509ClientVerification getX509ClientVerficationMode() const;

    std::string getBindAddress(ListenerProtocol p);
};
#endif // LISTENER_H
