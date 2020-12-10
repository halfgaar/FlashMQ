#ifndef MQTTPACKET_H
#define MQTTPACKET_H

#include "unistd.h"
#include <memory>
#include <vector>
#include <exception>

#include "forward_declarations.h"

#include "client.h"
#include "exceptions.h"
#include "types.h"
#include "subscriptionstore.h"


class MqttPacket
{
    std::vector<char> bites;
    size_t fixed_header_length = 0;
    Client_p sender;
    size_t pos = 0;
    ProtocolVersion protocolVersion = ProtocolVersion::None;

    char *readBytes(size_t length);
    char readByte();
    void writeByte(char b);
    uint16_t readTwoBytesToUInt16();
    size_t remainingAfterPos();

public:
    PacketType packetType = PacketType::Reserved;
    MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client_p &sender);
    MqttPacket(const ConnAck &connAck);
    MqttPacket(const SubAck &subAck);

    void handle(std::shared_ptr<SubscriptionStore> &subscriptionStore);
    void handleConnect();
    void handleSubscribe(std::shared_ptr<SubscriptionStore> &subscriptionStore);
    void handlePing();

    size_t getSize() { return bites.size(); }
    const std::vector<char> &getBites() { return bites; }

    Client_p getSender() const;
    void setSender(const Client_p &value);
};

#endif // MQTTPACKET_H
