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


#define CLIENT_BUFFER_SIZE 1024
#define CLIENT_MAX_BUFFER_SIZE 1048576
#define MQTT_HEADER_LENGH 2

class Client
{
    int fd;

    char *readbuf = NULL; // With many clients, it may not be smart to keep a (big) buffer around.
    size_t readBufsize = CLIENT_BUFFER_SIZE;
    int wi = 0;
    int ri = 0;

    char *writebuf = NULL; // With many clients, it may not be smart to keep a (big) buffer around.
    size_t writeBufsize = CLIENT_BUFFER_SIZE;
    int wwi = 0;
    int wri = 0;

    bool authenticated = false;
    bool connectPacketSeen = false;
    bool readyForWriting = false;
    bool readyForReading = true;
    bool disconnectWhenBytesWritten = false;

    std::string clientid;
    std::string username;
    uint16_t keepalive = 0;

    std::string will_topic;
    std::string will_payload;
    bool will_retain = false;
    char will_qos = 0;

    ThreadData_p threadData;
    std::mutex writeBufMutex;

    // Note: this is not the inverse of free space, because there can be non-used lead-in in the buffer!
    size_t getReadBufBytesUsed()
    {
        return wi - ri;
    };

    size_t getReadBufMaxWriteSize()
    {
        size_t available = readBufsize - wi;
        return available;
    }

    void growReadBuffer()
    {
        const size_t newBufSize = readBufsize * 2;
        char *readbuf = (char*)realloc(this->readbuf, newBufSize);
        if (readbuf == NULL)
            throw std::runtime_error("Memory allocation failure in growReadBuffer()");
        this->readbuf = readbuf;
        readBufsize = newBufSize;

        //std::cout << "New read buf size: " << readBufsize << std::endl;
    }

    size_t getWriteBufMaxWriteSize()
    {
        size_t available = writeBufsize - wwi;
        return available;
    }

    // Note: this is not the inverse of free space, because there can be non-used lead-in in the buffer!
    size_t getWriteBufBytesUsed()
    {
        return wwi - wri;
    };

    void growWriteBuffer(size_t add_size)
    {
        if (add_size == 0)
            return;

        const size_t grow_by = std::max<size_t>(add_size, writeBufsize*2);
        const size_t newBufSize = writeBufsize + grow_by;
        char *writebuf = (char*)realloc(this->writebuf, newBufSize);

        if (writebuf == NULL)
            throw std::runtime_error("Memory allocation failure in growWriteBuffer()");

        this->writebuf = writebuf;
        writeBufsize = newBufSize;

        //std::cout << "New write buf size: " << writeBufsize << std::endl;
    }

    void setReadyForWriting(bool val);
    void setReadyForReading(bool val);

public:
    Client(int fd, ThreadData_p threadData);
    ~Client();

    int getFd() { return fd;}
    void closeConnection();
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
    bool writeBufIntoFd();
    bool readyForDisconnecting() const { return disconnectWhenBytesWritten && wwi == wri && wwi == 0; }

    // Do this before calling an action that makes this client ready for writing, so that the EPOLLOUT will handle it.
    void setReadyForDisconnect() { disconnectWhenBytesWritten = true; }

    bool isDisconnected() const { return fd < 0; }

    std::string repr();

};

#endif // CLIENT_H
