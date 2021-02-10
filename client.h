#ifndef CLIENT_H
#define CLIENT_H

#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <iostream>
#include <time.h>

#include <openssl/ssl.h>

#include "forward_declarations.h"

#include "threaddata.h"
#include "mqttpacket.h"
#include "exceptions.h"
#include "cirbuf.h"
#include "types.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#define CLIENT_BUFFER_SIZE 1024 // Must be power of 2
#define MAX_PACKET_SIZE 268435461 // 256 MB + 5
#define MQTT_HEADER_LENGH 2

#define OPENSSL_ERROR_STRING_SIZE 256 // OpenSSL requires at least 256.
#define OPENSSL_WRONG_VERSION_NUMBER 336130315

enum class IoWrapResult
{
    Success = 0,
    Interrupted = 1,
    Wouldblock = 2,
    Disconnected = 3,
    Error = 4
};

/*
 * OpenSSL doc: "When a write function call has to be repeated because SSL_get_error(3) returned
 * SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, it must be repeated with the same arguments"
 */
struct IncompleteSslWrite
{
    const void *buf = nullptr;
    size_t nbytes = 0;

    IncompleteSslWrite() = default;
    IncompleteSslWrite(const void *buf, size_t nbytes);
    bool hasPendingWrite();

    void reset();
};

// TODO: give accepted addr, for showing in logs
class Client
{
    int fd;
    SSL *ssl = nullptr;
    bool sslAccepted = false;
    IncompleteSslWrite incompleteSslWrite;
    bool sslReadWantsWrite = false;
    bool sslWriteWantsRead = false;
    ProtocolVersion protocolVersion = ProtocolVersion::None;

    CirBuf readbuf;
    uint8_t readBufIsZeroCount = 0;

    CirBuf writebuf;
    uint8_t writeBufIsZeroCount = 0;

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
    Client(int fd, ThreadData_p threadData, SSL *ssl);
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
    ssize_t readWrap(int fd, void *buf, size_t nbytes, IoWrapResult *error);
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
    bool getCleanSession() { return cleanSession; }
    void assignSession(std::shared_ptr<Session> &session);
    std::shared_ptr<Session> getSession();
    void setDisconnectReason(const std::string &reason);

    void writePingResp();
    void writeMqttPacket(const MqttPacket &packet);
    void writeMqttPacketAndBlameThisClient(const MqttPacket &packet);
    ssize_t writeWrap(int fd, const void *buf, size_t nbytes, IoWrapResult *error);
    bool writeBufIntoFd();
    bool readyForDisconnecting() const { return disconnectWhenBytesWritten && writebuf.usedBytes() == 0; }

    // Do this before calling an action that makes this client ready for writing, so that the EPOLLOUT will handle it.
    void setReadyForDisconnect() { disconnectWhenBytesWritten = true; }

    std::string repr();
    bool keepAliveExpired();
    std::string getKeepAliveInfoString() const;

};

#endif // CLIENT_H
