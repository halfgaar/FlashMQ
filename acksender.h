#ifndef ACKSENDER_H
#define ACKSENDER_H

#include <stdint.h>

#include "types.h"

class AckSender
{
    uint8_t qos;
    uint16_t packetId;
    ProtocolVersion protocolVersion = ProtocolVersion::None;
    std::shared_ptr<Client> &client;
    ReasonCodes ackCode = ReasonCodes::Success;
    bool sent = false;
public:
    AckSender(const AckSender &other) = delete;
    AckSender(AckSender &&other) = delete;
    AckSender(uint8_t qos, uint16_t packetId, ProtocolVersion protocolVersion, std::shared_ptr<Client> &client);
    ~AckSender();
    void sendNow();
    void setAckCode(ReasonCodes ackCode);
};

#endif // ACKSENDER_H
