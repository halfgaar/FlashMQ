/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <openssl/err.h>

#include "listener.h"

#include "utils.h"
#include "exceptions.h"
#include "logger.h"
#include "configfileparser.h"

void Listener::isValid()
{
    if (isSsl())
    {
        if (port == 0)
        {
            if (connectionProtocol == ConnectionProtocol::WebsocketMqtt)
                port = 4443;
            else
                port = 8883;
        }

        if (acmeRedirectURL)
        {
            throw ConfigFileException("An SSL listener can't have an acme_redirect_url.");
        }

        if (!dropListener())
        {
            ConfigFileParser::checkFileExistsAndReadable("SSL fullchain", sslFullchain, 1024*1024);
            ConfigFileParser::checkFileExistsAndReadable("SSL privkey", sslPrivkey, 1024*1024);
            testSsl(sslFullchain, sslPrivkey);
        }

        testSslVerifyLocations(clientVerificationCaFile, clientVerificationCaDir, "Loading client_verification_ca_dir/client_verification_ca_file failed.");
    }
    else
    {
        if (port == 0)
        {
            if (connectionProtocol == ConnectionProtocol::AcmeOnly)
                port = 80;
            else if (connectionProtocol == ConnectionProtocol::WebsocketMqtt)
                port = 8080;
            else
                port = 1883;
        }

        if (dropOnAbsentCertificates)
        {
            throw ConfigFileException("Using drop_on_absent_certificate is only valid on SSL listeners; define privkey and fullchain.");
        }
    }

    if (protocol < ListenerProtocol::Unix && !unixSocketPath.empty())
    {
        throw ConfigFileException("Specifying 'unix_socket_path' for IP listeners is not allowed.");
    }

    if (protocol == ListenerProtocol::Unix)
    {
        if (unixSocketPath.empty())
            throw ConfigFileException("Option 'unix_socket_path' must be set for unix socket listeners.");

        if (!inet4BindAddress.empty() || !inet6BindAddress.empty())
            throw ConfigFileException("Specifying inet bind addresses is not allowed for unix socket listeners.");

        if (isSsl())
            throw ConfigFileException("TLS on domain sockets is not supported.");

        if (acmeRedirectURL)
            throw ConfigFileException("ACME redirect is not support on unix sockets");
    }

    if ((!clientVerificationCaDir.empty() || !clientVerificationCaFile.empty()) && !isSsl())
    {
        throw ConfigFileException("X509 client verification can only be done on TLS listeners.");
    }

    if (port <= 0 || port > 65534)
    {
        throw ConfigFileException(formatString("Port nr %d is not valid", port));
    }

    if (connectionProtocol == ConnectionProtocol::AcmeOnly && !acmeRedirectURL)
    {
        throw ConfigFileException("An ACME listener needs to have an acme_redirect_url");
    }
}

bool Listener::isSsl() const
{
    return (!sslFullchain.empty() || !sslPrivkey.empty());
}

bool Listener::isTcpNoDelay() const
{
    return this->tcpNoDelay;
}

bool Listener::isHaProxy() const
{
    return this->haproxy;
}

std::string Listener::getProtocolName() const
{
    if (protocol == ListenerProtocol::Unix)
    {
        return "unix socket";
    }
    if (connectionProtocol == ConnectionProtocol::AcmeOnly)
    {
        return "ACME-only";
    }
    else if (isSsl())
    {
        if (connectionProtocol == ConnectionProtocol::WebsocketMqtt)
            return "SSL websocket";
        else
            return "SSL TCP";
    }
    else
    {
        std::string answer;

        if (connectionProtocol == ConnectionProtocol::WebsocketMqtt)
            answer = "non-SSL websocket";
        else
            answer = "non-SSL TCP";

        if (acmeRedirectURL)
            answer.append(" with ACME redirect to ").append(acmeRedirectURL.value());
        return answer;
    }

    return "whoops";
}

void Listener::loadCertAndKeyFromConfig()
{
    if (!isSsl())
        return;

    if (!sslctx)
    {
        sslctx = std::make_unique<SslCtxManager>();
        sslctx->setMinimumTlsVersion(minimumTlsVersion);
        SSL_CTX_set_mode(sslctx->get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

        /*
         * Session cache requires active shutdown of SSL connections, which we don't have right now. We
         * might as well just turn the session cache off, at least until we do have session shutdown.
         */
        SSL_CTX_set_session_cache_mode(sslctx->get(), SSL_SESS_CACHE_OFF);
    }

    if (SSL_CTX_use_certificate_chain_file(sslctx->get(), sslFullchain.c_str()) != 1)
        throw std::runtime_error("Loading cert failed. This was after test loading the certificate, so is very unexpected.");
    if (SSL_CTX_use_PrivateKey_file(sslctx->get(), sslPrivkey.c_str(), SSL_FILETYPE_PEM) != 1)
        throw std::runtime_error("Loading key failed. This was after test loading the certificate, so is very unexpected.");

    {
        const char *ca_file = clientVerificationCaFile.empty() ? nullptr : clientVerificationCaFile.c_str();
        const char *ca_dir = clientVerificationCaDir.empty() ? nullptr : clientVerificationCaDir.c_str();

        if (ca_file || ca_dir)
        {
            if (SSL_CTX_load_verify_locations(sslctx->get(), ca_file, ca_dir) != 1)
            {
                ERR_print_errors_cb(logSslError, NULL);
                throw std::runtime_error("Loading client_verification_ca_dir/client_verification_ca_file failed. "
                                         "This was after test loading the certificate, so is very unexpected.");
            }
        }
    }
}

X509ClientVerification Listener::getX509ClientVerficationMode() const
{
    X509ClientVerification result = X509ClientVerification::None;
    const bool clientCADefined = !clientVerificationCaDir.empty() || !clientVerificationCaFile.empty();

    if (clientCADefined)
        result = X509ClientVerification::X509IsEnough;

    if (result >= X509ClientVerification::X509IsEnough && clientVerifictionStillDoAuthn)
        result = X509ClientVerification::X509AndUsernamePassword;

    return result;
}

bool Listener::dropListener() const
{
    if (!dropOnAbsentCertificates || !isSsl())
        return false;

    return access(sslPrivkey.c_str(), R_OK) != 0 && access(sslFullchain.c_str(), R_OK) != 0;
}

std::string Listener::getBindAddress(ListenerProtocol p)
{
    if (p == ListenerProtocol::IPv4)
    {
        if (inet4BindAddress.empty())
            return "0.0.0.0";
        return inet4BindAddress;
    }
    if (p == ListenerProtocol::IPv6)
    {
        if (inet6BindAddress.empty())
            return "::";
        return inet6BindAddress;
    }
    if (p == ListenerProtocol::Unix)
    {
        if (unixSocketPath.empty())
            throw std::runtime_error("Listener's unix socket path is empty.");
        return unixSocketPath;
    }
    return "";
}







