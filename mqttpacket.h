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

struct RemainingLength
{
    char bytes[4];
    int len = 0;
public:
    RemainingLength();
};

class MqttPacket
{
    std::vector<char> bites;
    size_t fixed_header_length = 0; // if 0, this packet does not contain the bytes of the fixed header.
    RemainingLength remainingLength;
    Client_p sender;
    char first_byte = 0;
    size_t pos = 0;
    ProtocolVersion protocolVersion = ProtocolVersion::None;

    char *readBytes(size_t length);
    char readByte();
    void writeByte(char b);
    void writeBytes(const char *b, size_t len);
    uint16_t readTwoBytesToUInt16();
    size_t remainingAfterPos();

    void calculateRemainingLength();

public:
    PacketType packetType = PacketType::Reserved;
    MqttPacket(char *buf, size_t len, size_t fixed_header_length, Client_p &sender); // Constructor for parsing incoming packets.

    // Constructor for outgoing packets. These may not allocate room for the fixed header, because we don't (always) know the length in advance.
    MqttPacket(const ConnAck &connAck);
    MqttPacket(const SubAck &subAck);
    MqttPacket(const Publish &publish);

    void handle(std::shared_ptr<SubscriptionStore> &subscriptionStore);
    void handleConnect();
    void handleSubscribe(std::shared_ptr<SubscriptionStore> &subscriptionStore);
    void handlePing();
    void handlePublish(std::shared_ptr<SubscriptionStore> &subscriptionStore);

    size_t getSizeIncludingNonPresentHeader() const;
    const std::vector<char> &getBites() const { return bites; }

    Client_p getSender() const;
    void setSender(const Client_p &value);

    bool containsFixedHeader() const;
    char getFirstByte() const;
    RemainingLength getRemainingLength() const;
};

#endif // MQTTPACKET_H
