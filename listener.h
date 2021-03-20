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

#ifndef LISTENER_H
#define LISTENER_H

#include <string>
#include <memory>

#include "sslctxmanager.h"

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
    std::string sslFullchain;
    std::string sslPrivkey;
    std::unique_ptr<SslCtxManager> sslctx;

    void isValid();
    bool isSsl() const;
    std::string getProtocolName() const;
    void loadCertAndKeyFromConfig();

    std::string getBindAddress(ListenerProtocol p);
};
#endif // LISTENER_H
