/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef IOWRAPPER_H
#define IOWRAPPER_H

#include "unistd.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#include <exception>

#include "forward_declarations.h"

#include "types.h"
#include "utils.h"
#include "logger.h"
#include "exceptions.h"

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
    Error = 4
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
    bool hasPendingWrite() const;

    void reset();
};

struct IncompleteWebsocketRead
{
    size_t frame_bytes_left = 0;
    char maskingKey[4];
    int maskingKeyI = 0;
    WebsocketOpcode opcode;

    void reset();
    bool sillWorkingOnFrame() const;
    char getNextMaskingByte();
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
    const size_t initialBufferSize;

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

    Logger *logger = Logger::getInstance();

    ssize_t websocketBytesToReadBuffer(void *buf, const size_t nbytes, IoWrapResult *error);
    ssize_t readOrSslRead(int fd, void *buf, size_t nbytes, IoWrapResult *error);
    ssize_t writeOrSslWrite(int fd, const void *buf, size_t nbytes, IoWrapResult *error);
    ssize_t writeAsMuchOfBufAsWebsocketFrame(const void *buf, size_t nbytes, WebsocketOpcode opcode = WebsocketOpcode::Binary);
public:
    IoWrapper(SSL *ssl, bool websocket, const size_t initialBufferSize, Client *parent);
    ~IoWrapper();

    void startOrContinueSslAccept();
    bool getSslReadWantsWrite() const;
    bool getSslWriteWantsRead() const;
    bool isSslAccepted() const;
    bool isSsl() const;
    bool hasPendingWrite() const;
    bool isWebsocket() const;
    WebsocketState getWebsocketState() const;

#ifndef NDEBUG
    void setFakeUpgraded();
#endif

    ssize_t readWebsocketAndOrSsl(int fd, void *buf, size_t nbytes, IoWrapResult *error);
    ssize_t writeWebsocketAndOrSsl(int fd, const void *buf, size_t nbytes, IoWrapResult *error);
};

#endif // IOWRAPPER_H
