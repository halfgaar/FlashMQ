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

#include "listener.h"

#include "utils.h"
#include "exceptions.h"

void Listener::isValid()
{
    if (isSsl())
    {
        if (port == 0)
        {
            if (websocket)
                port = 4443;
            else
                port = 8883;
        }

        testSsl(sslFullchain, sslPrivkey);
    }
    else
    {
        if (port == 0)
        {
            if (websocket)
                port = 8080;
            else
                port = 1883;
        }
    }

    if (port <= 0 || port > 65534)
    {
        throw ConfigFileException(formatString("Port nr %d is not valid", port));
    }
}

bool Listener::isSsl() const
{
    return (!sslFullchain.empty() || !sslPrivkey.empty());
}

bool Listener::isHaProxy() const
{
    return this->haproxy;
}

std::string Listener::getProtocolName() const
{
    if (isSsl())
    {
        if (websocket)
            return "SSL websocket";
        else
            return "SSL TCP";
    }
    else
    {
        if (websocket)
            return "non-SSL websocket";
        else
            return "non-SSL TCP";
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
        SSL_CTX_set_options(sslctx->get(), SSL_OP_NO_SSLv3); // TODO: config option
        SSL_CTX_set_options(sslctx->get(), SSL_OP_NO_TLSv1); // TODO: config option

        SSL_CTX_set_mode(sslctx->get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    }

    if (SSL_CTX_use_certificate_file(sslctx->get(), sslFullchain.c_str(), SSL_FILETYPE_PEM) != 1)
        throw std::runtime_error("Loading cert failed. This was after test loading the certificate, so is very unexpected.");
    if (SSL_CTX_use_PrivateKey_file(sslctx->get(), sslPrivkey.c_str(), SSL_FILETYPE_PEM) != 1)
        throw std::runtime_error("Loading key failed. This was after test loading the certificate, so is very unexpected.");
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
    return "";
}
