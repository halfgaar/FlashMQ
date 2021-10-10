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

#include "iowrapper.h"

#include "cassert"

#include "logger.h"
#include "client.h"

IncompleteSslWrite::IncompleteSslWrite(const void *buf, size_t nbytes) :
    buf(buf),
    nbytes(nbytes)
{

}

void IncompleteSslWrite::reset()
{
    buf = nullptr;
    nbytes = 0;
}

bool IncompleteSslWrite::hasPendingWrite() const
{
    return buf != nullptr;
}

void IncompleteWebsocketRead::reset()
{
    maskingKeyI = 0;
    memset(maskingKey,0, 4);
    frame_bytes_left = 0;
    opcode = WebsocketOpcode::Unknown;
}

bool IncompleteWebsocketRead::sillWorkingOnFrame() const
{
    return frame_bytes_left > 0;
}

char IncompleteWebsocketRead::getNextMaskingByte()
{
    return maskingKey[maskingKeyI++ % 4];
}

IoWrapper::IoWrapper(SSL *ssl, bool websocket, const size_t initialBufferSize, Client *parent) :
    parentClient(parent),
    initialBufferSize(initialBufferSize),
    ssl(ssl),
    websocket(websocket),
    websocketPendingBytes(websocket ? initialBufferSize : 0),
    websocketWriteRemainder(websocket ? initialBufferSize : 0)
{

}

IoWrapper::~IoWrapper()
{
    if (ssl)
    {
        // I don't do SSL_shutdown(), because I don't want to keep the session, plus, that takes active de-negiotation, so it can't be done
        // in the destructor.
        SSL_free(ssl);
    }
}

void IoWrapper::startOrContinueSslAccept()
{
    ERR_clear_error();
    int accepted = SSL_accept(ssl);
    char sslErrorBuf[OPENSSL_ERROR_STRING_SIZE];
    if (accepted <= 0)
    {
        int err = SSL_get_error(ssl, accepted);

        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        {
            parentClient->setReadyForWriting(err == SSL_ERROR_WANT_WRITE);
            return;
        }

        unsigned long error_code = ERR_get_error();

        ERR_error_string(error_code, sslErrorBuf);
        std::string errorMsg(sslErrorBuf, OPENSSL_ERROR_STRING_SIZE);

        if (error_code == OPENSSL_WRONG_VERSION_NUMBER)
            errorMsg = "Wrong protocol version number. Probably a non-SSL connection on SSL socket.";

        //ERR_print_errors_cb(logSslError, NULL);
        throw std::runtime_error("Problem accepting SSL socket: " + errorMsg);
    }
    parentClient->setReadyForWriting(false); // Undo write readiness that may have have happened during SSL handshake
    sslAccepted = true;
}

bool IoWrapper::getSslReadWantsWrite() const
{
    return this->sslReadWantsWrite;
}

bool IoWrapper::getSslWriteWantsRead() const
{
    return sslWriteWantsRead;
}

bool IoWrapper::isSslAccepted() const
{
    return this->sslAccepted;
}

bool IoWrapper::isSsl() const
{
    return this->ssl != nullptr;
}

bool IoWrapper::hasPendingWrite() const
{
    return incompleteSslWrite.hasPendingWrite() || websocketWriteRemainder.usedBytes() > 0;
}

bool IoWrapper::isWebsocket() const
{
    return websocket;
}

WebsocketState IoWrapper::getWebsocketState() const
{
    return websocketState;
}

#ifndef NDEBUG
/**
 * @brief IoWrapper::setFakeUpgraded marks this wrapper as upgraded. This is for fuzzing, so to bypass the sha1 protected handshake
 * of websockets.
 */
void IoWrapper::setFakeUpgraded()
{
    websocketState = WebsocketState::Upgraded;
}
#endif

/**
 * @brief SSL and non-SSL sockets behave differently. For one, reading 0 doesn't mean 'disconnected' with an SSL
 * socket. This wrapper unifies behavor for the caller.
 *
 * @param fd
 * @param buf
 * @param nbytes
 * @param error is an out-argument with the result.
 * @return
 */
ssize_t IoWrapper::readOrSslRead(int fd, void *buf, size_t nbytes, IoWrapResult *error)
{
    *error = IoWrapResult::Success;
    ssize_t n = 0;
    if (!ssl)
    {
        n = read(fd, buf, nbytes);
        if (n < 0)
        {
            if (errno == EINTR)
                *error = IoWrapResult::Interrupted;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                *error = IoWrapResult::Wouldblock;
            else
                check<std::runtime_error>(n);
        }
        else if (n == 0)
        {
            *error = IoWrapResult::Disconnected;
        }
    }
    else
    {
        this->sslReadWantsWrite = false;
        ERR_clear_error();
        char sslErrorBuf[OPENSSL_ERROR_STRING_SIZE];
        n = SSL_read(ssl, buf, nbytes);

        if (n <= 0)
        {
            int err = SSL_get_error(ssl, n);
            unsigned long error_code = ERR_get_error();

            // See https://www.openssl.org/docs/man1.1.1/man3/SSL_get_error.html "BUGS" why EOF is seen as SSL_ERROR_SYSCALL.
            if (err == SSL_ERROR_ZERO_RETURN || (err == SSL_ERROR_SYSCALL && errno == 0))
            {
                *error = IoWrapResult::Disconnected;
            }
            else if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
                *error = IoWrapResult::Wouldblock;
                if (err == SSL_ERROR_WANT_WRITE)
                {
                    sslReadWantsWrite = true;
                    parentClient->setReadyForWriting(true);
                }
                n = -1;
            }
            else
            {
                if (err == SSL_ERROR_SYSCALL)
                {
                    // I don't actually know if OpenSSL hides this or passes EINTR on. The docs say
                    // 'Some non-recoverable, fatal I/O error occurred' for SSL_ERROR_SYSCALL, so it
                    // implies EINTR is not included?
                    if (errno == EINTR)
                        *error = IoWrapResult::Interrupted;
                    else
                    {
                        char *err = strerror(errno);
                        std::string msg(err);
                        throw std::runtime_error("SSL read error: " + msg);
                    }
                }
                ERR_error_string(error_code, sslErrorBuf);
                std::string errorString(sslErrorBuf, OPENSSL_ERROR_STRING_SIZE);
                ERR_print_errors_cb(logSslError, NULL);
                throw std::runtime_error("SSL socket error reading: " + errorString);
            }
        }
    }

    return n;
}

// SSL and non-SSL sockets behave differently. This wrapper unifies behavor for the caller.
ssize_t IoWrapper::writeOrSslWrite(int fd, const void *buf, size_t nbytes, IoWrapResult *error)
{
    *error = IoWrapResult::Success;
    ssize_t n = 0;

    if (!ssl)
    {
        // A write on a socket with count=0 is unspecified.
        assert(nbytes > 0);

        n = write(fd, buf, nbytes);
        if (n < 0)
        {
            if (errno == EINTR)
                *error = IoWrapResult::Interrupted;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                *error = IoWrapResult::Wouldblock;
            else
                check<std::runtime_error>(n);
        }
    }
    else
    {
        const void *buf_ = buf;
        size_t nbytes_ = nbytes;

        /*
         * OpenSSL doc: When a write function call has to be repeated because SSL_get_error(3) returned
         * SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, it must be repeated with the same arguments
         */
        if (this->incompleteSslWrite.hasPendingWrite())
        {
            buf_ = this->incompleteSslWrite.buf;
            nbytes_ = this->incompleteSslWrite.nbytes;
        }

        // OpenSSL: "You should not call SSL_write() with num=0, it will return an error"
        assert(nbytes_ > 0);

        this->sslWriteWantsRead = false;
        this->incompleteSslWrite.reset();

        ERR_clear_error();
        char sslErrorBuf[OPENSSL_ERROR_STRING_SIZE];
        n = SSL_write(ssl, buf_, nbytes_);

        if (n <= 0)
        {
            int err = SSL_get_error(ssl, n);
            unsigned long error_code = ERR_get_error();
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
                logger->logf(LOG_DEBUG, "SSL Write is incomplete: %d. Will be retried later.", err);
                *error = IoWrapResult::Wouldblock;
                IncompleteSslWrite sslAction(buf_, nbytes_);
                this->incompleteSslWrite = sslAction;
                if (err == SSL_ERROR_WANT_READ)
                    this->sslWriteWantsRead = true;
                n = 0;
            }
            else
            {
                if (err == SSL_ERROR_SYSCALL)
                {
                    // I don't actually know if OpenSSL hides this or passes EINTR on. The docs say
                    // 'Some non-recoverable, fatal I/O error occurred' for SSL_ERROR_SYSCALL, so it
                    // implies EINTR is not included?
                    if (errno == EINTR)
                        *error = IoWrapResult::Interrupted;
                    else
                    {
                        char *err = strerror(errno);
                        std::string msg(err);
                        throw std::runtime_error(msg);
                    }
                }
                ERR_error_string(error_code, sslErrorBuf);
                std::string errorString(sslErrorBuf, OPENSSL_ERROR_STRING_SIZE);
                ERR_print_errors_cb(logSslError, NULL);
                throw std::runtime_error("SSL socket error writing: " + errorString);
            }
        }
    }

    return n;
}

// Use a small intermediate buffer to write (partial) websocket frames to our normal read buffer. MQTT is already a frames protocol, so we don't
// care about websocket frames being incomplete.
ssize_t IoWrapper::readWebsocketAndOrSsl(int fd, void *buf, size_t nbytes, IoWrapResult *error)
{
    if (!websocket)
    {
        return readOrSslRead(fd, buf, nbytes, error);
    }
    else
    {
        ssize_t n = 0;
        while (websocketPendingBytes.freeSpace() > 0 && (n = readOrSslRead(fd, websocketPendingBytes.headPtr(), websocketPendingBytes.maxWriteSize(), error)) != 0)
        {
            if (n > 0)
                websocketPendingBytes.advanceHead(n);
            if (n < 0)
                break; // signal/error handling is done by the caller, so we just stop.

            if (websocketState == WebsocketState::NotUpgraded && websocketPendingBytes.freeSpace() == 0)
            {
                if (websocketPendingBytes.getSize() * 2 <= 8192)
                    websocketPendingBytes.doubleSize();
                else
                    throw ProtocolError("Trying to exceed websocket buffer. Probably not valid websocket traffic.");
            }
        }

        const bool hasWebsocketPendingBytes = websocketPendingBytes.usedBytes() > 0;

        // When some or all the data has been read, we can continue.
        if (!(*error == IoWrapResult::Wouldblock || *error == IoWrapResult::Success) && !hasWebsocketPendingBytes)
            return n;

        if (hasWebsocketPendingBytes)
        {
            n = 0;

            if (websocketState == WebsocketState::NotUpgraded)
            {
                try
                {
                    std::string websocketKey;
                    int websocketVersion;
                    if (parseHttpHeader(websocketPendingBytes, websocketKey, websocketVersion))
                    {
                        if (websocketKey.empty())
                            throw BadHttpRequest("No websocket key specified.");
                        if (websocketVersion != 13)
                            throw BadWebsocketVersionException("Websocket version 13 required.");

                        const std::string acceptString = generateWebsocketAcceptString(websocketKey);

                        std::string answer = generateWebsocketAnswer(acceptString);
                        parentClient->writeText(answer);
                        websocketState = WebsocketState::Upgrading;
                        websocketPendingBytes.reset();
                        websocketPendingBytes.resetSize(initialBufferSize);
                        *error = IoWrapResult::Success;
                    }
                }
                catch (BadWebsocketVersionException &ex)
                {
                    std::string response = generateInvalidWebsocketVersionHttpHeaders(13);
                    parentClient->writeText(response);
                    parentClient->setDisconnectReason("Invalid websocket version");
                    parentClient->setReadyForDisconnect();
                }
                catch (BadHttpRequest &ex) // Should should also properly deal with attempt at HTTP2 with PRI.
                {
                    std::string response = generateBadHttpRequestReponse(ex.what());
                    parentClient->writeText(response);
                    const std::string reason = formatString("Invalid websocket start: %s", ex.what());
                    parentClient->setDisconnectReason(reason);
                    parentClient->setReadyForDisconnect();
                }
            }
            else
            {
                n = websocketBytesToReadBuffer(buf, nbytes, error);

                if (n > 0)
                    *error = IoWrapResult::Success;
                else if (n == 0 && *error != IoWrapResult::Disconnected)
                    *error = IoWrapResult::Wouldblock;
            }
        }

        return n;
    }
}

/**
 * @brief IoWrapper::websocketBytesToReadBuffer takes the payload from a websocket packet and puts it in the 'normal' read buffer, the
 * buffer that contains the MQTT bytes.
 * @param buf
 * @param nbytes
 * @return
 */
ssize_t IoWrapper::websocketBytesToReadBuffer(void *buf, const size_t nbytes, IoWrapResult *error)
{
    const ssize_t targetBufMaxSize = nbytes;
    ssize_t nbytesRead = 0;

    while (websocketPendingBytes.usedBytes() >= WEBSOCKET_MIN_HEADER_BYTES_NEEDED
           && incompleteWebsocketRead.frame_bytes_left <= websocketPendingBytes.usedBytes()
           && nbytesRead < targetBufMaxSize)
    {
        // This block decodes the header.
        if (!incompleteWebsocketRead.sillWorkingOnFrame())
        {
            const uint8_t byte1 = websocketPendingBytes.peakAhead(0);
            const uint8_t byte2 = websocketPendingBytes.peakAhead(1);
            bool masked = !!(byte2 & 0b10000000);
            uint8_t reserved = (byte1 & 0b01110000) >> 4;
            WebsocketOpcode opcode = (WebsocketOpcode)(byte1 & 0b00001111);
            const uint8_t payloadLength = byte2 & 0b01111111;
            size_t realPayloadLength = payloadLength;
            uint64_t extendedPayloadLengthLength = 0;
            uint8_t headerLength = masked ? 6 : 2;

            if (payloadLength == 126)
                extendedPayloadLengthLength = 2;
            else if (payloadLength == 127)
                extendedPayloadLengthLength = 8;
            headerLength += extendedPayloadLengthLength;

            //if (!masked)
            //    throw ProtocolError("Client must send masked websocket bytes.");

            if (reserved != 0)
                throw ProtocolError("Reserved bytes in header must be 0.");

            if (headerLength > websocketPendingBytes.usedBytes())
                return nbytesRead;

            uint64_t extendedPayloadLength = 0;

            int i = 2;
            int shift = extendedPayloadLengthLength * 8;
            while (shift > 0)
            {
                shift -= 8;
                uint8_t byte = websocketPendingBytes.peakAhead(i++);
                extendedPayloadLength += (byte << shift);
            }

            if (extendedPayloadLength > 0)
                realPayloadLength = extendedPayloadLength;

            if (headerLength > websocketPendingBytes.usedBytes())
                return nbytesRead;

            if (masked)
            {
                for (int j = 0; j < 4; j++)
                {
                    incompleteWebsocketRead.maskingKey[j] = websocketPendingBytes.peakAhead(i++);
                }
            }

            assert(i == headerLength);
            assert(headerLength <= websocketPendingBytes.usedBytes());
            websocketPendingBytes.advanceTail(headerLength);

            incompleteWebsocketRead.frame_bytes_left = realPayloadLength;
            incompleteWebsocketRead.opcode = opcode;
        }

        if (incompleteWebsocketRead.opcode == WebsocketOpcode::Binary)
        {
            // The following reads one websocket frame max: it will continue with the previous, or start a new one, which it may or may not finish.
            size_t targetBufI = 0;
            char *targetBuf = &static_cast<char*>(buf)[nbytesRead];
            while(websocketPendingBytes.usedBytes() > 0 && incompleteWebsocketRead.frame_bytes_left > 0 && nbytesRead < targetBufMaxSize)
            {
                const size_t asManyBytesOfThisFrameAsPossible = std::min<size_t>(websocketPendingBytes.maxReadSize(), incompleteWebsocketRead.frame_bytes_left);
                const size_t maxReadSize = std::min<size_t>(asManyBytesOfThisFrameAsPossible, targetBufMaxSize - nbytesRead);
                assert(maxReadSize > 0);
                assert(static_cast<ssize_t>(maxReadSize) + nbytesRead <= targetBufMaxSize);
                for (size_t x = 0; x < maxReadSize; x++)
                {
                    targetBuf[targetBufI++] = websocketPendingBytes.tailPtr()[x] ^ incompleteWebsocketRead.getNextMaskingByte();
                }
                websocketPendingBytes.advanceTail(maxReadSize);
                incompleteWebsocketRead.frame_bytes_left -= maxReadSize;
                nbytesRead += maxReadSize;
            }
        }
        else if (incompleteWebsocketRead.opcode == WebsocketOpcode::Ping)
        {
            // A ping MAY have user data, which needs to be ponged back. Pings contain no MQTT data, so nbytesRead is
            // not touched, nor are we writing to the client's MQTT buffer.

            if (incompleteWebsocketRead.frame_bytes_left <= websocketPendingBytes.usedBytes())
            {
                logger->logf(LOG_INFO, "Ponging websocket");

                // Constructing a new temporary buffer because I need the reponse in one frame for writeAsMuchOfBufAsWebsocketFrame().
                std::vector<char> response(incompleteWebsocketRead.frame_bytes_left);
                websocketPendingBytes.read(response.data(), response.size());

                websocketWriteRemainder.ensureFreeSpace(response.size());
                writeAsMuchOfBufAsWebsocketFrame(response.data(), response.size(), WebsocketOpcode::Pong);
                parentClient->setReadyForWriting(true);
            }
        }
        else if (incompleteWebsocketRead.opcode == WebsocketOpcode::Close)
        {
            // MUST be a 2-byte unsigned integer (in network byte order) representing a status code with value /code/ defined
            if (incompleteWebsocketRead.frame_bytes_left <= websocketPendingBytes.usedBytes()
                && incompleteWebsocketRead.frame_bytes_left >= 2)
            {
                const uint8_t msb = *websocketPendingBytes.tailPtr() ^ incompleteWebsocketRead.getNextMaskingByte();
                websocketPendingBytes.advanceTail(1);
                const uint8_t lsb = *websocketPendingBytes.tailPtr() ^ incompleteWebsocketRead.getNextMaskingByte();
                websocketPendingBytes.advanceTail(1);

                const uint16_t code = msb << 8 | lsb;

                // An actual MQTT disconnect doesn't send websocket close frames, or perhaps after the MQTT
                // disconnect when it doesn't matter anymore. So, when users close the tab or stuff like that,
                // we can consider it a closed transport i.e. failed connection. This means will messages
                // will be sent.
                parentClient->setDisconnectReason(websocketCloseCodeToString(code));
                *error = IoWrapResult::Disconnected;

                // There may be a UTF8 string with a reason in the packet still, but ignoring that for now.
                incompleteWebsocketRead.reset();
            }
        }
        else
        {
            // Specs: "MQTT Control Packets MUST be sent in WebSocket binary data frames. If any other type of data frame is
            // received the recipient MUST close the Network Connection [MQTT-6.0.0-1]".
            throw ProtocolError(formatString("Websocket frames must be 'binary' or 'ping'. Received: %d", incompleteWebsocketRead.opcode));
        }

        if (!incompleteWebsocketRead.sillWorkingOnFrame())
            incompleteWebsocketRead.reset();
    }
    assert(nbytesRead <= static_cast<ssize_t>(nbytes));

    return nbytesRead;
}

/**
 * @brief IoWrapper::writeAsMuchOfBufAsWebsocketFrame writes buf or part of buf as websocket frame to websocketWriteRemainder
 * @param buf
 * @param nbytes. The amount of bytes. Can be 0, for just an empty websocket frame.
 * @return
 */
ssize_t IoWrapper::writeAsMuchOfBufAsWebsocketFrame(const void *buf, size_t nbytes, WebsocketOpcode opcode)
{
    // We do allow pong frames to generate a zero payload packet, but for binary, that's not necessary.
    if (nbytes == 0 && opcode == WebsocketOpcode::Binary)
        return 0;

    ssize_t nBytesReal = 0;

    // We normally wrap each write in a frame, but if a previous one didn't fit in the system's write buffers, we're still working on it.
    if (websocketWriteRemainder.freeSpace() > WEBSOCKET_MAX_SENDING_HEADER_SIZE)
    {
        uint8_t extended_payload_length_num_bytes = 0;
        uint8_t payload_length = 0;
        if (nbytes < 126)
            payload_length = nbytes;
        else if (nbytes >= 126 && nbytes <= 0xFFFF)
        {
            payload_length = 126;
            extended_payload_length_num_bytes = 2;
        }
        else if (nbytes > 0xFFFF)
        {
            payload_length = 127;
            extended_payload_length_num_bytes = 8;
        }

        int x = 0;
        char header[WEBSOCKET_MAX_SENDING_HEADER_SIZE];
        header[x++] = (0b10000000 | static_cast<char>(opcode));
        header[x++] = payload_length;

        const int header_length = x + extended_payload_length_num_bytes;

        // This block writes the extended payload length.
        nBytesReal = std::min<size_t>(nbytes, websocketWriteRemainder.freeSpace() - header_length);
        const uint64_t nbytes64 = nBytesReal;
        for (int z = extended_payload_length_num_bytes - 1; z >= 0; z--)
        {
            header[x++] = (nbytes64 >> (z*8)) & 0xFF;
        }
        assert(x <= WEBSOCKET_MAX_SENDING_HEADER_SIZE);
        assert(x == header_length);

        websocketWriteRemainder.write(header, header_length);
        websocketWriteRemainder.write(buf, nBytesReal);
    }

    return nBytesReal;
}

/*
 *  Mqtt docs: "A single WebSocket data frame can contain multiple or partial MQTT Control Packets. The receiver
 *  MUST NOT assume that MQTT Control Packets are aligned on WebSocket frame boundaries [MQTT-6.0.0-2]." We
 *  make use of that here, and wrap each write in a frame.
 *
 *  It's can legitimately return a number of bytes written AND error with 'would block'. So, no need to do that
 *  repeating of the write thing that SSL_write() has.
 */
ssize_t IoWrapper::writeWebsocketAndOrSsl(int fd, const void *buf, size_t nbytes, IoWrapResult *error)
{
    if (websocketState != WebsocketState::Upgraded)
    {
        if (websocket && websocketState == WebsocketState::Upgrading)
            websocketState = WebsocketState::Upgraded;

        return writeOrSslWrite(fd, buf, nbytes, error);
    }
    else
    {
        ssize_t nBytesReal = writeAsMuchOfBufAsWebsocketFrame(buf, nbytes);

        ssize_t n = 0;
        while (websocketWriteRemainder.usedBytes() > 0)
        {
            n = writeOrSslWrite(fd, websocketWriteRemainder.tailPtr(), websocketWriteRemainder.maxReadSize(), error);

            if (n > 0)
                websocketWriteRemainder.advanceTail(n);
            if (n < 0)
                break;
        }

        if (n > 0)
            return nBytesReal;

        return n;
    }
}
