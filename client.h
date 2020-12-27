#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <iostream>

#include "forward_declarations.h"

#include "threaddata.h"
#include "mqttpacket.h"
#include "exceptions.h"
#include "cirbuf.h"


#define CLIENT_BUFFER_SIZE 1024 // Must be power of 2
#define MAX_PACKET_SIZE 268435461 // 256 MB + 5
#define MQTT_HEADER_LENGH 2

class Client
{
    int fd;

    CirBuf readbuf;

    CirBuf writebuf;

    bool authenticated = false;
    bool connectPacketSeen = false;
    bool readyForWriting = false;
    bool readyForReading = true;
    bool disconnectWhenBytesWritten = false;
    bool disconnecting = false;

    std::string clientid;
    std::string username;
    uint16_t keepalive = 0;

    std::string will_topic;
    std::string will_payload;
    bool will_retain = false;
    char will_qos = 0;

    ThreadData_p threadData;
    std::mutex writeBufMutex;


    void setReadyForWriting(bool val);
    void setReadyForReading(bool val);

public:
    Client(int fd, ThreadData_p threadData);
    Client(const Client &other) = delete;
    Client(Client &&other) = delete;
    ~Client();

    int getFd() { return fd;}
    void markAsDisconnecting();
    bool readFdIntoBuffer();
    bool bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn, Client_p &sender);
    void setClientProperties(const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive);
    void setWill(const std::string &topic, const std::string &payload, bool retain, char qos);
    void setAuthenticated(bool value) { authenticated = value;}
    bool getAuthenticated() { return authenticated; }
    bool hasConnectPacketSeen() { return connectPacketSeen; }
    ThreadData_p getThreadData() { return threadData; }
    std::string &getClientId() { return this->clientid; }

    void writePingResp();
    void writeMqttPacket(const MqttPacket &packet);
    void writeMqttPacketAndBlameThisClient(const MqttPacket &packet);
    bool writeBufIntoFd();
    bool readyForDisconnecting() const { return disconnectWhenBytesWritten && writebuf.usedBytes() == 0; }

    // Do this before calling an action that makes this client ready for writing, so that the EPOLLOUT will handle it.
    void setReadyForDisconnect() { disconnectWhenBytesWritten = true; }

    std::string repr();

};

#endif // CLIENT_H
