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
    size_t bufsize = CLIENT_BUFFER_SIZE;
    int wi = 0;
    int ri = 0;

    bool authenticated = false;
    bool connectPacketSeen = false;
    std::string clientid;
    std::string username;
    uint16_t keepalive = 0;

    ThreadData_p threadData;

    size_t getBufBytesUsed()
    {
        return wi - ri;
    };

    size_t getMaxWriteSize()
    {
        size_t available = bufsize - getBufBytesUsed();
        return available;
    }

    void growBuffer()
    {
        const size_t newBufSize = bufsize * 2;
        readbuf = (char*)realloc(readbuf, newBufSize);
        bufsize = newBufSize;
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


};

typedef std::shared_ptr<Client> Client_p;

#endif // CLIENT_H
