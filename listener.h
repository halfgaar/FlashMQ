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
#include <optional>

#include "sslctxmanager.h"
#include "enums.h"

enum class ListenerProtocol
{
    IPv46,
    IPv4,
    IPv6,
    Unix
};

struct Listener
{
    /*
     * We track this per listener so that if you isolate clients to a specific listener, you have
     * control over on which thread it ends up.
     */
    size_t next_thread_index = 0;

    ListenerProtocol protocol = ListenerProtocol::IPv46;
    std::string inet4BindAddress;
    std::string inet6BindAddress;
    std::string unixSocketPath;
    int port = 0;
    ConnectionProtocol connectionProtocol = ConnectionProtocol::Mqtt;
    bool tcpNoDelay = false;
    bool haproxy = false;
    std::string sslFullchain;
    std::string sslPrivkey;
    std::string clientVerificationCaFile;
    std::string clientVerificationCaDir;
    bool clientVerifictionStillDoAuthn = false;
    std::unique_ptr<SslCtxManager> sslctx;
    AllowListenerAnonymous allowAnonymous = AllowListenerAnonymous::None;
    std::optional<std::string> acmeRedirectURL;
    TLSVersion minimumTlsVersion = TLSVersion::TLSv1_1;
    std::optional<OverloadMode> overloadMode;
    bool dropOnAbsentCertificates = false;
    std::optional<uint32_t> maxBufferSize;

    void isValid();
    bool isSsl() const;
    bool isHaProxy() const;
    bool isTcpNoDelay() const;
    std::string getProtocolName() const;
    void loadCertAndKeyFromConfig();
    X509ClientVerification getX509ClientVerficationMode() const;
    bool dropListener() const;

    std::string getBindAddress(ListenerProtocol p);
};
#endif // LISTENER_H
