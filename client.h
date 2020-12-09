#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include "threaddata.h"
#include "mqttpacket.h"
#include "exceptions.h"

#define CLIENT_BUFFER_SIZE 1024
#define MQTT_HEADER_LENGH 2

class ThreadData;
typedef  std::shared_ptr<ThreadData> ThreadData_p;

class MqttPacket;

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
    std::string clientid;
    std::string username;
    uint16_t keepalive = 0;

    ThreadData_p threadData;

    size_t getReadBufBytesUsed()
    {
        return wi - ri;
    };

    size_t getReadBufMaxWriteSize()
    {
        size_t available = readBufsize - getReadBufBytesUsed();
        return available;
    }

    void growReadBuffer()
    {
        const size_t newBufSize = readBufsize * 2;
        readbuf = (char*)realloc(readbuf, newBufSize);
        readBufsize = newBufSize;
    }

    size_t getWriteBufMaxWriteSize()
    {
        size_t available = writeBufsize - getWriteBufBytesUsed();
        return available;
    }

    size_t getWriteBufBytesUsed()
    {
        return wwi - wri;
    };

    void growWriteBuffer(size_t add_size)
    {
        if (add_size == 0)
            return;

        const size_t newBufSize = writeBufsize + add_size;
        writebuf = (char*)realloc(writebuf, newBufSize);
        writeBufsize = newBufSize;
    }

public:
    Client(int fd, ThreadData_p threadData);
    ~Client();

    int getFd() { return fd;}
    bool readFdIntoBuffer();
    bool bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn);
    void setClientProperties(const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive);
    void setAuthenticated(bool value) { authenticated = value;}
    bool getAuthenticated() { return authenticated; }
    bool hasConnectPacketSeen() { return connectPacketSeen; }

    void writePingResp();
    void writeMqttPacket(MqttPacket &packet);
    bool writeBufIntoFd();

    std::string repr();

};

typedef std::shared_ptr<Client> Client_p;

#endif // CLIENT_H
