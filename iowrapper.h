/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#ifndef IOWRAPPER_H
#define IOWRAPPER_H

#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <exception>

#include "forward_declarations.h"

#include "logger.h"
#include "haproxy.h"
#include "cirbuf.h"
#include "x509manager.h"
#include "fmqsockaddr.h"

#define WEBSOCKET_MIN_HEADER_BYTES_NEEDED 2
#define WEBSOCKET_MAX_SENDING_HEADER_SIZE 10

#define OPENSSL_ERROR_STRING_SIZE 256 // OpenSSL requires at least 256.
#define OPENSSL_WRONG_VERSION_NUMBER 336130315

enum class IoWrapResult
{
    Success = 0,
    Interrupted = 1,
    Wouldblock = 2,
    Disconnected = 3,
    Error = 4,
    WantRead = 5
};

enum class WebsocketOpcode
{
    Continuation = 0x00,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
    Unknown = 0xF
};

/**
 * @brief The IncompleteSslWrite struct facilities the SSL retry
 *
 * OpenSSL doc: "When a write function call has to be repeated because SSL_get_error(3) returned
 * SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, it must be repeated with the same arguments"
 *
 * Note that we use SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER.
 */
struct IncompleteSslWrite
{
    bool valid = false;
    size_t nbytes = 0;

    IncompleteSslWrite() = default;
    IncompleteSslWrite(size_t nbytes);
    bool hasPendingWrite() const;

    void reset();
};

struct IncompleteWebsocketRead
{
    size_t frame_bytes_left = 0;
    char maskingKey[4];
    unsigned int maskingKeyI = 0;
    WebsocketOpcode opcode;

    void reset();
    bool sillWorkingOnFrame() const;
    char getNextMaskingByte();
    IncompleteWebsocketRead();
};

enum class WebsocketState
{
    NotUpgraded,
    Upgrading,
    Upgraded
};

/**
 * @brief provides a unified wrapper for SSL and websockets to read() and write().
 *
 *
 */
class IoWrapper
{
    Client *parentClient;

    SSL *ssl = nullptr;
    bool sslAccepted = false;
    IncompleteSslWrite incompleteSslWrite;
    bool sslReadWantsWrite = false;
    bool sslWriteWantsRead = false;

    bool websocket;
    WebsocketState websocketState = WebsocketState::NotUpgraded;
    CirBuf websocketPendingBytes;
    IncompleteWebsocketRead incompleteWebsocketRead;
    CirBuf websocketWriteRemainder;

    bool _needsHaProxyParsing = false;

    Logger *logger = Logger::getInstance();

    ssize_t websocketBytesToReadBuffer(void *buf, const size_t nbytes, IoWrapResult *error);
    ssize_t readOrSslRead(int fd, void *buf, size_t nbytes, IoWrapResult *error);
    ssize_t writeOrSslWrite(int fd, const void *buf, size_t nbytes, IoWrapResult *error);
    ssize_t writeAsMuchOfBufAsWebsocketFrame(const void *buf, const size_t nbytes, WebsocketOpcode opcode = WebsocketOpcode::Binary);

    void startOrContinueSslConnect();
    void startOrContinueSslAccept();

public:
    IoWrapper(SSL *ssl, bool websocket, const size_t initialBufferSize, Client *parent);
    ~IoWrapper();

    void startOrContinueSslHandshake();
    bool getSslReadWantsWrite() const;
    bool getSslWriteWantsRead() const;
    bool isSslAccepted() const;
    bool isSsl() const;
    void setSslVerify(int mode, const std::string &hostname);
    bool hasPendingWrite() const;
    bool hasProcessedBufferedBytesToRead() const;
    bool isWebsocket() const;
    WebsocketState getWebsocketState() const;
    X509Manager getPeerCertificate() const;
    const char *getSslVersion() const;

    bool needsHaProxyParsing() const;
    std::tuple<HaProxyConnectionType, std::optional<FMQSockaddr>> readHaProxyData(int fd);
    void setHaProxy(bool val);

#ifndef NDEBUG
    void setFakeUpgraded();
#endif

    ssize_t readWebsocketAndOrSsl(int fd, void *buf, size_t nbytes, IoWrapResult *error);
    ssize_t writeWebsocketAndOrSsl(int fd, const void *buf, size_t nbytes, IoWrapResult *error);

    void resetBuffersIfEligible();
};

#endif // IOWRAPPER_H
