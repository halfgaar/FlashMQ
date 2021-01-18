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

struct QueuedQosPacket
{
    uint16_t id;
    std::shared_ptr<MqttPacket> packet;
};

class Session
{
    std::weak_ptr<Client> client;
    std::string client_id;
    std::list<QueuedQosPacket> qosPacketQueue; // Using list because it's easiest to maintain order [MQTT-4.6.0-6]
    std::mutex qosQueueMutex;
    uint16_t nextPacketId = 0;
    ssize_t qosQueueBytes = 0;
    Logger *logger = Logger::getInstance();
public:
    Session();
    Session(const Session &other) = delete;
    Session(Session &&other) = delete;

    const std::string &getClientId() const { return client_id; }
    bool clientDisconnected() const;
    std::shared_ptr<Client> makeSharedClient() const;
    void assignActiveConnection(std::shared_ptr<Client> &client);
    void writePacket(const MqttPacket &packet, char max_qos);
    void clearQosMessage(uint16_t packet_id);
    void sendPendingQosMessages();
};

#endif // SESSION_H
