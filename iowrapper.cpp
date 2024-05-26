/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "iowrapper.h"

#include <cassert>
#include <string.h>
#include <openssl/x509v3.h>

#include "logger.h"
#include "client.h"
#include "utils.h"
#include "exceptions.h"
#include "threadglobals.h"
#include "settings.h"

IncompleteSslWrite::IncompleteSslWrite(size_t nbytes) :
    valid(true),
    nbytes(nbytes)
{

}

void IncompleteSslWrite::reset()
{
    valid = false;
    nbytes = 0;
}

bool IncompleteSslWrite::hasPendingWrite() const
{
    return valid;
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

IncompleteWebsocketRead::IncompleteWebsocketRead()
{
    reset();
}

IoWrapper::IoWrapper(SSL *ssl, bool websocket, const size_t initialBufferSize, Client *parent) :
    parentClient(parent),
    ssl(ssl),
    websocket(websocket),
    websocketPendingBytes(websocket ? initialBufferSize : 0),
    websocketWriteRemainder(websocket ? initialBufferSize : 0)
{

}

IoWrapper::~IoWrapper()
{
    // I don't do SSL_shutdown(), because I don't want to keep the session, plus, that takes active de-negiotation, so it can't be done
    // in the destructor.
    SSL_free(ssl);
    ssl = nullptr;
}

void IoWrapper::startOrContinueSslHandshake()
{
    if (parentClient->isOutgoingConnection())
        startOrContinueSslConnect();
    else
        startOrContinueSslAccept();
}

void IoWrapper::startOrContinueSslConnect()
{
    assert(ssl);
    ERR_clear_error();
    char sslErrorBuf[OPENSSL_ERROR_STRING_SIZE];
    int connected = SSL_connect(ssl);
    if (connected <= 0)
    {
        int err = SSL_get_error(ssl, connected);

        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        {
            parentClient->setReadyForWriting(err == SSL_ERROR_WANT_WRITE);
            return;
        }

        unsigned long error_code = ERR_get_error();

        ERR_error_string(error_code, sslErrorBuf);
        std::string errorMsg(sslErrorBuf, OPENSSL_ERROR_STRING_SIZE);

        if (error_code == 0)
            errorMsg = "Error code was 0. Are you really connecting to a TLS socket?";

        throw std::runtime_error("Problem connecting to SSL socket: " + errorMsg);

    }
    parentClient->setReadyForWriting(false); // Undo write readiness that may have have happened during SSL handshake
    sslAccepted = true;
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

static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
    SSL *ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));

    const int mode = SSL_get_verify_mode(ssl);

    if (mode == SSL_VERIFY_NONE)
        return 1;

    std::vector<char> buf(512);

    X509 *err_cert = X509_STORE_CTX_get_current_cert(ctx);
    int err = X509_STORE_CTX_get_error(ctx);
    int depth = X509_STORE_CTX_get_error_depth(ctx);

    X509_NAME_oneline(X509_get_subject_name(err_cert), buf.data(), 256);
    const std::string subject_name(buf.data());

    /*
     * Explicity catch long chains, to avoid other random chain errors.
     */
    if (depth > 50)
    {
        preverify_ok = 0;
        err = X509_V_ERR_CERT_CHAIN_TOO_LONG;
        X509_STORE_CTX_set_error(ctx, err);
    }

    Logger *logger = Logger::getInstance();

    if (!preverify_ok)
    {
        X509_NAME_oneline(X509_get_issuer_name(err_cert), buf.data(), 256);
        const std::string issuer(buf.data());
        logger->logf(LOG_ERR, "X509 verify error. Num=%d. Error: %s. Depth=%d. Subject = %s. Issuer = %s",
                     err, X509_verify_cert_error_string(err), depth, subject_name.c_str(), issuer.c_str());
    }

    return preverify_ok;
}

void IoWrapper::setSslVerify(int mode, const std::string &hostname)
{
    assert(mode == SSL_VERIFY_PEER || mode == SSL_VERIFY_NONE);

    if (!ssl)
        return;

    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    if (!hostname.empty())
    {
        if (!SSL_set1_host(ssl, hostname.c_str()))
            throw std::runtime_error("Failed setting hostname of SSL context.");

        if (SSL_set_tlsext_host_name(ssl, hostname.c_str()) != 1)
            throw std::runtime_error("Failed setting SNI hostname of SSL context.");
    }

    SSL_set_verify(ssl, mode, verify_callback);
}

bool IoWrapper::hasPendingWrite() const
{
    return incompleteSslWrite.hasPendingWrite() || websocketWriteRemainder.usedBytes() > 0;
}

/**
 * @brief IoWrapper::hasProcessedBufferedBytesToRead needs to be used for when event-based IO (epoll) won't inform you there are pending bytes.
 * @return
 *
 * When the system sockets is readable, epoll will say so. But when you buffer that with SSL and/or websockets, it can happen that you
 * don't fully read that, because the client buffers are full. So, use this function on 'buffer full' conditions to determine that you
 * need to grow the buffer and read again.
 */
bool IoWrapper::hasProcessedBufferedBytesToRead() const
{
    bool result = false;

    if (ssl)
        result |= SSL_pending(ssl) > 0;

    /*
     * Note that this is tecnhically not 100% correct. If the only bytes are part of a header, doing a read will actually
     * result in 0 bytes. But, for the intended purpose at time of writing (see git), we can get away with this.
     */
    if (websocket)
        result |= websocketPendingBytes.usedBytes() > 0;

    return result;
}

bool IoWrapper::isWebsocket() const
{
    return websocket;
}

WebsocketState IoWrapper::getWebsocketState() const
{
    return websocketState;
}

X509Manager IoWrapper::getPeerCertificate() const
{
    X509Manager result(this->ssl);
    return result;
}

bool IoWrapper::needsHaProxyParsing() const
{
    return _needsHaProxyParsing;
}

/**
 * @brief IoWrapper::readHaProxyData Reads the PROXY protocol header in one go. It must fit in one TCP segment.
 * @param fd
 * @param addr
 */
HaProxyConnectionType IoWrapper::readHaProxyData(int fd, struct sockaddr *addr)
{
    _needsHaProxyParsing = false;

    constexpr const int sig_len = 12;
    constexpr const int min_len = 16;
    const std::string signature("\r\n\r\n\0\r\nQUIT\n", sig_len);

    char buf[min_len];
    memset(buf, 0, min_len);

    read_ha_proxy_helper(fd, buf, min_len);

    int i = 0;

    const std::string received_signature(buf, sig_len);
    i += sig_len;

    if (received_signature != signature)
        throw std::runtime_error("Invalid HAProxy signature.");

    const uint8_t ver_cmd = buf[i++];
    const uint8_t fam_prot = buf[i++];
    const uint8_t family = (fam_prot & 0xF0) >> 4;
    const uint8_t prot = fam_prot & 0x0F;

    const uint8_t ver = (ver_cmd & 0xF0) >> 4;
    const uint8_t cmd = ver_cmd & 0x0F;

    const uint8_t len_high = buf[i++];
    const uint8_t len_low = buf[i++];
    const uint16_t len = len_high | len_low;

    if (len > sizeof(union proxy_addr))
        throw std::runtime_error("Hacker be gone.");

    union proxy_addr proxy_addr;
    memset(&proxy_addr, 0, sizeof(union proxy_addr));
    read_ha_proxy_helper(fd, &proxy_addr, len);

    if (ver != 2)
        throw std::runtime_error("Only HAProxy protocol version 2 is supported");

    if (cmd == 0)
        return HaProxyConnectionType::Local;

    if (cmd != 1)
        throw std::runtime_error("HAProxy command must be 1");

    if (prot == 0)
        return HaProxyConnectionType::Local;

    if (prot > 2)
        throw std::runtime_error("Invalid protocol in HAProxy.");

    if (family == 1)
    {
        struct sockaddr_in *a = reinterpret_cast<struct sockaddr_in*>(addr);
        a->sin_family = AF_INET;
        a->sin_addr.s_addr = proxy_addr.ipv4_addr.src_addr;
        a->sin_port = proxy_addr.ipv4_addr.src_port;
    }
    else if (family == 2)
    {
        struct sockaddr_in6 *a = reinterpret_cast<struct sockaddr_in6*>(addr);
        a->sin6_family = AF_INET6;
        memcpy(&a->sin6_addr, proxy_addr.ipv6_addr.src_addr, 16);
        a->sin6_port = proxy_addr.ipv6_addr.src_port;
    }

    return HaProxyConnectionType::Remote;
}

void IoWrapper::setHaProxy(bool val)
{
    this->_needsHaProxyParsing = val;
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
        size_t nbytes_ = nbytes;

        /*
         * OpenSSL doc: When a write function call has to be repeated because SSL_get_error(3) returned
         * SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, it must be repeated with the same arguments
         */
        if (this->incompleteSslWrite.hasPendingWrite())
        {
            nbytes_ = this->incompleteSslWrite.nbytes;
        }

        // OpenSSL: "You should not call SSL_write() with num=0, it will return an error"
        assert(nbytes_ > 0);

        this->sslWriteWantsRead = false;
        this->incompleteSslWrite.reset();

        ERR_clear_error();
        char sslErrorBuf[OPENSSL_ERROR_STRING_SIZE];
        n = SSL_write(ssl, buf, nbytes_);

        if (n <= 0)
        {
            int err = SSL_get_error(ssl, n);
            unsigned long error_code = ERR_get_error();
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
                logger->logf(LOG_DEBUG, "SSL Write is incomplete: %d. Will be retried later.", err);
                *error = IoWrapResult::Wouldblock;
                IncompleteSslWrite sslAction(nbytes_);
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

/**
 * @brief Read the fd into buf. For websockets, reads the fd into an intermediate buffer and decodes the result to buf. MQTT is already a frames
 * protocol, so we don't care about websocket frames being incomplete.
 * @param fd
 * @param buf
 * @param nbytes
 * @param error May still be set when bytes are written.
 * @return number of bytes read, despite also possibly having an error. This is possible when there was still buffered data when you called it.
 *
 * Because of its buffered nature, it can legitimately return a number of bytes read AND set an error. So, the
 * interface is somewhat different from normal 'read()' syscalls.
 */
ssize_t IoWrapper::readWebsocketAndOrSsl(int fd, void *buf, size_t nbytes, IoWrapResult *error)
{
    if (!websocket)
    {
        return readOrSslRead(fd, buf, nbytes, error);
    }

    ssize_t n = 0;
    while (websocketPendingBytes.freeSpace() > 0 && (n = readOrSslRead(fd, websocketPendingBytes.headPtr(), websocketPendingBytes.maxWriteSize(), error)) != 0)
    {
        if (n > 0)
            websocketPendingBytes.advanceHead(n);
        else if (n < 0)
        {
            if (*error == IoWrapResult::Interrupted)
                return n;
            break; // On other errors, like 'would block' it depends on our 'pending bytes' what we will tell the caller.
        }

        // Make sure we either always have enough space for a next loop iteration, or stop reading the fd.
        if (websocketPendingBytes.freeSpace() == 0)
        {
            if (websocketState == WebsocketState::NotUpgraded)
            {
                if (websocketPendingBytes.getSize() * 2 <= 8192)
                    websocketPendingBytes.doubleSize();
                else
                    throw BadClientException("Trying to exceed websocket buffer. Probably not valid websocket traffic.");
            }
            else
            {
                const Settings *settings = ThreadGlobals::getSettings();
                if (websocketPendingBytes.getSize() * 2 <= settings->clientMaxWriteBufferSize)
                {
                    websocketPendingBytes.doubleSize();
                }
                else
                {
                    break;
                }
            }
        }
    }

    const bool hasWebsocketPendingBytes = websocketPendingBytes.usedBytes() > 0;

    // When some or all the data has been read, we can continue.
    if (!(*error == IoWrapResult::Wouldblock || *error == IoWrapResult::Success) && !hasWebsocketPendingBytes)
        return n; // TODO: I guess because of the error condition check this is never > 0? It needs a bit of clarification.

    // This should never happen, because of the above check, but I'm leaving it in just in case.
    if (!hasWebsocketPendingBytes)
        return n;

    if (websocketState == WebsocketState::NotUpgraded)
    {
        try
        {
            std::string websocketKey;
            int websocketVersion;
            std::string subprotocol;
            std::string xRealIp;

            if (!parseHttpHeader(websocketPendingBytes, websocketKey, websocketVersion, subprotocol, xRealIp))
                return 0;

            if (websocketKey.empty())
                throw BadHttpRequest("No websocket key specified.");
            if (websocketVersion != 13)
                throw BadWebsocketVersionException("Websocket version 13 required.");

            const std::string acceptString = generateWebsocketAcceptString(websocketKey);

            const Settings *settings = ThreadGlobals::getSettings();
            const size_t initialBufferSize = settings->clientInitialBufferSize;

            std::string answer = generateWebsocketAnswer(acceptString, subprotocol);
            parentClient->writeText(answer);
            websocketState = WebsocketState::Upgrading;
            websocketPendingBytes.reset();
            websocketPendingBytes.resetSize(initialBufferSize);
            *error = IoWrapResult::Success;

            if (!xRealIp.empty())
                parentClient->setAddr(xRealIp);

            return 0;
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

        return 0;
    }

    n = websocketBytesToReadBuffer(buf, nbytes, error);

    if (*error != IoWrapResult::Disconnected)
        *error = websocketPendingBytes.usedBytes() == 0 ? IoWrapResult::Wouldblock : IoWrapResult::WantRead;

    return n;
}

/**
 * @brief IoWrapper::websocketBytesToReadBuffer takes the payload from a websocket packet and puts it in the 'normal' read buffer, the
 * buffer that contains the MQTT bytes.
 * @param buf
 * @param nbytes
 * @return the number of bytes read. Can be 0 when only websocket meta or empty frames are processed.
 *
 * When websocketPendingBytes still has bytes when we return, the following could have happened:
 * - We don't have enough bytes to know how long the frame is.
 * - On ping/close, we don't have enough bytes of the frame.
 */
ssize_t IoWrapper::websocketBytesToReadBuffer(void *buf, const size_t nbytes, IoWrapResult *error)
{
    const ssize_t targetBufMaxSize = nbytes;
    ssize_t nbytesRead = 0;

    int iter_count = 0;

    auto log_spin = [&](const std::string &id) {
        logger->log(LOG_ERR) << std::boolalpha << "Websocket spin loop in " << id << " detected. Please report at https://github.com/halfgaar/FlashMQ. Variables: "
                             << "usedBytes=" << websocketPendingBytes.usedBytes() << ". nbytesRead=" << nbytesRead << ". targetBufMaxSize=" << targetBufMaxSize
                             << ". sillWorkingOnFrame=" << incompleteWebsocketRead.sillWorkingOnFrame() << ". "
                             << "frameBytesLeft=" << incompleteWebsocketRead.frame_bytes_left << ". opcode="
                             << std::hex << static_cast<int>(incompleteWebsocketRead.opcode) << ".";
        throw std::runtime_error("Websocket spin loop detected. Please report at https://github.com/halfgaar/FlashMQ with log.");
    };

    while (websocketPendingBytes.usedBytes() > 0 && nbytesRead < targetBufMaxSize)
    {
        if (iter_count++ >= 1000000)
            log_spin("A");

        // This block decodes the header.
        if (!incompleteWebsocketRead.sillWorkingOnFrame())
        {
            if (websocketPendingBytes.usedBytes() < WEBSOCKET_MIN_HEADER_BYTES_NEEDED)
                break;

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
            //    throw BadClientException("Client must send masked websocket bytes.");

            if (reserved != 0)
                throw BadClientException("Reserved bytes in header must be 0.");

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

                if (iter_count++ >= 1000000)
                    log_spin("B");
            }
        }
        else if (incompleteWebsocketRead.opcode == WebsocketOpcode::Ping)
        {
            // A ping MAY have user data, which needs to be ponged back. Pings contain no MQTT data, so nbytesRead is
            // not touched, nor are we writing to the client's MQTT buffer.

            const Settings *settings = ThreadGlobals::getSettings();

            // Because these internal websocket frames don't contain bytes for the client, we need to allow them to fit
            // fully in websocketPendingBytes, otherwise you can get stuck.
            if (incompleteWebsocketRead.frame_bytes_left > (settings->clientMaxWriteBufferSize / 2))
                throw BadClientException("The option 'client_max_write_buffer_size / 2' is lower than the ping frame we're are supposed to pong back. Abusing client?");

            if (incompleteWebsocketRead.frame_bytes_left > websocketPendingBytes.usedBytes())
                break;

            logger->logf(LOG_DEBUG, "Ponging websocket");

            const size_t bufLen = std::min<size_t>(incompleteWebsocketRead.frame_bytes_left, websocketPendingBytes.usedBytes());

            // Constructing a new temporary buffer because I need the reponse in one frame for writeAsMuchOfBufAsWebsocketFrame().
            std::vector<char> masked_payload(bufLen);
            websocketPendingBytes.read(masked_payload.data(), masked_payload.size());

            std::vector<char> response(bufLen);
            for (size_t i = 0; i < bufLen; i++)
            {
                response[i] = masked_payload[i] ^ incompleteWebsocketRead.getNextMaskingByte();
            }

            websocketWriteRemainder.ensureFreeSpace(response.size() + WEBSOCKET_MAX_SENDING_HEADER_SIZE);
            writeAsMuchOfBufAsWebsocketFrame(response.data(), response.size(), WebsocketOpcode::Pong);
            parentClient->setReadyForWriting(true);

            incompleteWebsocketRead.frame_bytes_left -= bufLen;
        }
        else if (incompleteWebsocketRead.opcode == WebsocketOpcode::Pong)
        {
            // See ping comments

            const Settings *settings = ThreadGlobals::getSettings();

            if (incompleteWebsocketRead.frame_bytes_left > (settings->clientMaxWriteBufferSize / 2))
                throw BadClientException("The option 'client_max_write_buffer_size / 2' is lower than the pong frame we're getting. Abusing client?");

            if (incompleteWebsocketRead.frame_bytes_left > websocketPendingBytes.usedBytes())
                break;

            const size_t ponglen = std::min<size_t>(incompleteWebsocketRead.frame_bytes_left, websocketPendingBytes.usedBytes());
            logger->log(LOG_DEBUG) << "Received websocket pong (with length " << ponglen << ")? We never sent a ping...";
            std::vector<char> payload(ponglen);
            websocketPendingBytes.read(payload.data(), payload.size());
            incompleteWebsocketRead.frame_bytes_left -= ponglen;
        }
        else if (incompleteWebsocketRead.opcode == WebsocketOpcode::Close)
        {
            const Settings *settings = ThreadGlobals::getSettings();

            // Because these internal websocket frames don't contain bytes for the client, we need to allow them to fit
            // fully in websocketPendingBytes, otherwise you can get stuck.
            if (incompleteWebsocketRead.frame_bytes_left > (settings->clientMaxWriteBufferSize / 2))
                throw BadClientException("Websocket close frame is too big.");

            if (incompleteWebsocketRead.frame_bytes_left > websocketPendingBytes.usedBytes())
                break;

            std::string websocketCloseString = "Websocket close without reason code";

            if (incompleteWebsocketRead.frame_bytes_left >= 2)
            {
                // If there is payload, MUST be a 2-byte unsigned integer (in network byte order) representing a status code with value /code/ defined
                const uint8_t msb = *websocketPendingBytes.tailPtr() ^ incompleteWebsocketRead.getNextMaskingByte();
                websocketPendingBytes.advanceTail(1);
                const uint8_t lsb = *websocketPendingBytes.tailPtr() ^ incompleteWebsocketRead.getNextMaskingByte();
                websocketPendingBytes.advanceTail(1);

                const uint16_t code = msb << 8 | lsb;

                websocketCloseString = websocketCloseCodeToString(code);
            }

            // An actual MQTT disconnect doesn't send websocket close frames, or perhaps after the MQTT
            // disconnect when it doesn't matter anymore. So, when users close the tab or stuff like that,
            // we can consider it a closed transport i.e. failed connection. This means will messages
            // will be sent.
            parentClient->setDisconnectReason(websocketCloseString);
            *error = IoWrapResult::Disconnected;

            // There may be a UTF8 string with a reason in the packet still, but ignoring that for now.
            incompleteWebsocketRead.reset();
            websocketPendingBytes.reset();
        }
        else
        {
            // Specs: "MQTT Control Packets MUST be sent in WebSocket binary data frames. If any other type of data frame is
            // received the recipient MUST close the Network Connection [MQTT-6.0.0-1]".
            std::ostringstream opcode_oss;
            opcode_oss << "Unsupported websocket frame type: " << std::hex << static_cast<int>(incompleteWebsocketRead.opcode);
            throw BadClientException(opcode_oss.str());
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
ssize_t IoWrapper::writeAsMuchOfBufAsWebsocketFrame(const void *buf, const size_t nbytes, WebsocketOpcode opcode)
{
    // We do allow pong frames to generate a zero payload packet, but for binary, that's not necessary.
    if (nbytes == 0 && opcode == WebsocketOpcode::Binary)
        return 0;

    const Settings *settings = ThreadGlobals::getSettings();

    websocketWriteRemainder.ensureFreeSpace(nbytes + WEBSOCKET_MAX_SENDING_HEADER_SIZE, settings->clientMaxWriteBufferSize);

    const uint32_t bytesFree = websocketWriteRemainder.freeSpace();
    const size_t bodyBytesAvailable = bytesFree < WEBSOCKET_MAX_SENDING_HEADER_SIZE ? 0 : bytesFree - WEBSOCKET_MAX_SENDING_HEADER_SIZE;
    const ssize_t nBytesReal = std::min<size_t>(nbytes, bodyBytesAvailable);

    // We normally wrap each write in a frame, but if a previous one didn't fit in the system's write buffers, we're still working on it.
    if (websocketWriteRemainder.freeSpace() > WEBSOCKET_MAX_SENDING_HEADER_SIZE)
    {
        uint8_t extended_payload_length_num_bytes = 0;
        uint8_t payload_length = 0;
        if (nBytesReal < 126)
            payload_length = nBytesReal;
        else if (nBytesReal >= 126 && nBytesReal <= 0xFFFF)
        {
            payload_length = 126;
            extended_payload_length_num_bytes = 2;
        }
        else if (nBytesReal > 0xFFFF)
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

/**
 * @brief write the buffer to the fd, potentially as websocket frame.
 * @param fd
 * @param buf
 * @param nbytes
 * @param error May still be set when bytes are written.
 * @return number of bytes written, despite also possibly having an error.
 *
 * Because of its buffered nature (for websockets), it can legitimately return a number of bytes written AND set an
 * error. So, the interface is somewhat different from normal 'write()' syscalls.
 *
 * This also means there is no need to do that repeating of the write thing that SSL_write() has when there is
 * still buffered data. Just obey the 'wouldblock' error.
 *
 * Mqtt docs: "A single WebSocket data frame can contain multiple or partial MQTT Control Packets. The receiver
 * MUST NOT assume that MQTT Control Packets are aligned on WebSocket frame boundaries [MQTT-6.0.0-2]." We
 * make use of that here, and wrap each write in a frame.
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
        const ssize_t nBytesReal = writeAsMuchOfBufAsWebsocketFrame(buf, nbytes);

        while (websocketWriteRemainder.usedBytes() > 0)
        {
            const ssize_t n = writeOrSslWrite(fd, websocketWriteRemainder.tailPtr(), websocketWriteRemainder.maxReadSize(), error);

            if (n > 0)
                websocketWriteRemainder.advanceTail(n);
            else
                break;
        }

        return nBytesReal;
    }
}

void IoWrapper::resetBuffersIfEligible()
{
    const Settings *settings = ThreadGlobals::getSettings();
    const size_t initialBufferSize = settings->clientInitialBufferSize;

    const size_t sz = websocket ? initialBufferSize : 0;
    websocketPendingBytes.resetSizeIfEligable(sz),
    websocketWriteRemainder.resetSizeIfEligable(sz);
}
