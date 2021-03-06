#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <iostream>
#include <time.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "forward_declarations.h"

#include "threaddata.h"
#include "mqttpacket.h"
#include "exceptions.h"
#include "cirbuf.h"
#include "types.h"
#include "iowrapper.h"

#define MQTT_HEADER_LENGH 2


// TODO: give accepted addr, for showing in logs
class Client
{
    friend class IoWrapper;

    int fd;

    ProtocolVersion protocolVersion = ProtocolVersion::None;

    const size_t initialBufferSize = 0;
    const size_t maxPacketSize = 0;

    IoWrapper ioWrapper;

    CirBuf readbuf;
    CirBuf writebuf;

    bool authenticated = false;
    bool connectPacketSeen = false;
    bool readyForWriting = false;
    bool readyForReading = true;
    bool disconnectWhenBytesWritten = false;
    bool disconnecting = false;
    std::string disconnectReason;
    time_t lastActivity = time(NULL);

    std::string clientid;
    std::string username;
    uint16_t keepalive = 0;
    bool cleanSession = false;

    std::string will_topic;
    std::string will_payload;
    bool will_retain = false;
    char will_qos = 0;

    ThreadData_p threadData;
    std::mutex writeBufMutex;

    std::shared_ptr<Session> session;

    Logger *logger = Logger::getInstance();

    void setReadyForWriting(bool val);
    void setReadyForReading(bool val);

public:
    Client(int fd, ThreadData_p threadData, SSL *ssl, bool websocket, std::shared_ptr<Settings> settings);
    Client(const Client &other) = delete;
    Client(Client &&other) = delete;
    ~Client();

    int getFd() { return fd;}
    bool isSslAccepted() const;
    bool isSsl() const;
    bool getSslReadWantsWrite() const;
    bool getSslWriteWantsRead() const;

    void startOrContinueSslAccept();
    void markAsDisconnecting();
    bool readFdIntoBuffer();
    bool bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn, Client_p &sender);
    void setClientProperties(ProtocolVersion protocolVersion, const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive, bool cleanSession);
    void setWill(const std::string &topic, const std::string &payload, bool retain, char qos);
    void clearWill();
    void setAuthenticated(bool value) { authenticated = value;}
    bool getAuthenticated() { return authenticated; }
    bool hasConnectPacketSeen() { return connectPacketSeen; }
    ThreadData_p getThreadData() { return threadData; }
    std::string &getClientId() { return this->clientid; }
    const std::string &getUsername() const { return this->username; }
    bool getCleanSession() { return cleanSession; }
    void assignSession(std::shared_ptr<Session> &session);
    std::shared_ptr<Session> getSession();
    void setDisconnectReason(const std::string &reason);

    void writeText(const std::string &text);
    void writePingResp();
    void writeMqttPacket(const MqttPacket &packet);
    void writeMqttPacketAndBlameThisClient(const MqttPacket &packet);
    bool writeBufIntoFd();
    bool readyForDisconnecting() const { return disconnectWhenBytesWritten && writebuf.usedBytes() == 0; }

    // Do this before calling an action that makes this client ready for writing, so that the EPOLLOUT will handle it.
    void setReadyForDisconnect() { disconnectWhenBytesWritten = true; }

    std::string repr();
    bool keepAliveExpired();
    std::string getKeepAliveInfoString() const;
    void resetBuffersIfEligible();

};

#endif // CLIENT_H
