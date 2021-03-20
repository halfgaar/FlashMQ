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

#ifndef SESSION_H
#define SESSION_H

#include <memory>
#include <list>
#include <mutex>

#include "forward_declarations.h"
#include "logger.h"

// TODO make settings. But, num of packets can't exceed 65536, because the counter is 16 bit.
#define MAX_QOS_MSG_PENDING_PER_CLIENT 32
#define MAX_QOS_BYTES_PENDING_PER_CLIENT 4096

// TODO make setting
#define EXPIRE_SESSION_AFTER 1209600

struct QueuedQosPacket
{
    uint16_t id;
    std::shared_ptr<MqttPacket> packet;
};

class Session
{
    std::weak_ptr<Client> client;
    ThreadData_p thread;
    std::string client_id;
    std::string username;
    std::list<QueuedQosPacket> qosPacketQueue; // Using list because it's easiest to maintain order [MQTT-4.6.0-6]
    std::mutex qosQueueMutex;
    uint16_t nextPacketId = 0;
    ssize_t qosQueueBytes = 0;
    time_t lastTouched = time(NULL);
    Logger *logger = Logger::getInstance();

public:
    Session();
    Session(const Session &other) = delete;
    Session(Session &&other) = delete;
    ~Session();

    const std::string &getClientId() const { return client_id; }
    bool clientDisconnected() const;
    std::shared_ptr<Client> makeSharedClient() const;
    void assignActiveConnection(std::shared_ptr<Client> &client);
    void writePacket(const MqttPacket &packet, char max_qos);
    void clearQosMessage(uint16_t packet_id);
    void sendPendingQosMessages();
    void touch(time_t val = 0);
    bool hasExpired();
};

#endif // SESSION_H
