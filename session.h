#ifndef SESSION_H
#define SESSION_H

#include <memory>
#include <unordered_map>
#include <mutex>

#include "forward_declarations.h"
#include "logger.h"

// TODO make settings
#define MAX_QOS_MSG_PENDING_PER_CLIENT 32
#define MAX_QOS_BYTES_PENDING_PER_CLIENT 4096

class Session
{
    std::weak_ptr<Client> client;
    std::unordered_map<uint16_t, std::shared_ptr<MqttPacket>> qosPacketQueue; // TODO: because the max queue length should remain low-ish, perhaps a vector is better here.
    std::mutex qosQueueMutex;
    uint16_t nextPacketId = 0;
    ssize_t qosQueueBytes = 0;
    Logger *logger = Logger::getInstance();
public:
    Session();
    Session(const Session &other) = delete;
    Session(Session &&other) = delete;

    bool clientDisconnected() const;
    std::shared_ptr<Client> makeSharedClient() const;
    void assignActiveConnection(std::shared_ptr<Client> &client);
    void writePacket(const MqttPacket &packet);
    void clearQosMessage(uint16_t packet_id);
    void sendPendingQosMessages();
};

#endif // SESSION_H
