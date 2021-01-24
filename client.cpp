#include "client.h"

#include <cstring>
#include <sstream>
#include <iostream>
#include <cassert>

#include "logger.h"

Client::Client(int fd, ThreadData_p threadData, SSL *ssl) :
    fd(fd),
    ssl(ssl),
    readbuf(CLIENT_BUFFER_SIZE),
    writebuf(CLIENT_BUFFER_SIZE),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Client::~Client()
{
    if (disconnectReason.empty())
        disconnectReason = "not specified";

    logger->logf(LOG_NOTICE, "Removing client '%s'. Reason: %s", repr().c_str(), disconnectReason.c_str());
    if (epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL) != 0)
        logger->logf(LOG_ERR, "Removing fd %d of client '%s' from epoll produced error: %s", fd, repr().c_str(), strerror(errno));
    if (ssl)
    {
        // I don't do SSL_shutdown(), because I don't want to keep the session, plus, that takes active de-negiotation, so it can't be done
        // in the destructor.
        SSL_free(ssl);
    }
    close(fd);
}

bool Client::isSslAccepted() const
{
    return sslAccepted;
}

bool Client::isSsl() const
{
    return this->ssl != nullptr;
}

bool Client::getSslReadWantsWrite() const
{
    return this->sslReadWantsWrite;
}

bool Client::getSslWriteWantsRead() const
{
    return this->sslWriteWantsRead;
}

void Client::startOrContinueSslAccept()
{
    ERR_clear_error();
    int accepted = SSL_accept(ssl);
    char sslErrorBuf[OPENSSL_ERROR_STRING_SIZE];
    if (accepted <= 0)
    {
        int err = SSL_get_error(ssl, accepted);

        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        {
            setReadyForWriting(err == SSL_ERROR_WANT_WRITE);
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
    setReadyForWriting(false); // Undo write readiness that may have have happened during SSL handshake
    sslAccepted = true;
}

// Causes future activity on the client to cause a disconnect.
void Client::markAsDisconnecting()
{
    if (disconnecting)
        return;

    disconnecting = true;
}

// SSL and non-SSL sockets behave differently. For one, reading 0 doesn't mean 'disconnected' with an SSL
// socket. This wrapper unifies behavor for the caller.
ssize_t Client::readWrap(int fd, void *buf, size_t nbytes, IoWrapResult *error)
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
                    setReadyForWriting(true);
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

// false means any kind of error we want to get rid of the client for.
bool Client::readFdIntoBuffer()
{
    if (disconnecting)
        return false;

    IoWrapResult error = IoWrapResult::Success;
    int n = 0;
    while (readbuf.freeSpace() > 0 && (n = readWrap(fd, readbuf.headPtr(), readbuf.maxWriteSize(), &error)) != 0)
    {
        if (n > 0)
        {
            readbuf.advanceHead(n);
        }

        if (error == IoWrapResult::Interrupted)
            continue;
        if (error == IoWrapResult::Wouldblock)
            break;

        // Make sure we either always have enough space for a next call of this method, or stop reading the fd.
        if (readbuf.freeSpace() == 0)
        {
            if (readbuf.getSize() * 2 < MAX_PACKET_SIZE)
            {
                readbuf.doubleSize();
            }
            else
            {
                setReadyForReading(false);
                break;
            }
        }
    }

    if (error == IoWrapResult::Disconnected)
    {
        return false;
    }

    lastActivity = time(NULL);

    return true;
}

void Client::writeMqttPacket(const MqttPacket &packet)
{
    std::lock_guard<std::mutex> locker(writeBufMutex);

    // We have to allow big packets, yet don't allow a slow loris subscriber to grow huge write buffers. This
    // could be enhanced a lot, but it's a start.
    const uint32_t growBufMaxTo = std::min<int>(packet.getSizeIncludingNonPresentHeader() * 1000, MAX_PACKET_SIZE);

    // Grow as far as we can. We have to make room for one MQTT packet.
    while (packet.getSizeIncludingNonPresentHeader() > writebuf.freeSpace() && writebuf.getSize() < growBufMaxTo)
    {
        writebuf.doubleSize();
    }

    // And drop a publish when it doesn't fit, even after resizing. This means we do allow pings. And
    // QoS packet are queued and limited elsewhere.
    if (packet.packetType == PacketType::PUBLISH && packet.getQos() == 0 && packet.getSizeIncludingNonPresentHeader() > writebuf.freeSpace())
    {
        return;
    }

    if (!packet.containsFixedHeader())
    {
        writebuf.headPtr()[0] = packet.getFirstByte();
        writebuf.advanceHead(1);
        RemainingLength r = packet.getRemainingLength();

        ssize_t len_left = r.len;
        int src_i = 0;
        while (len_left > 0)
        {
            const size_t len = std::min<int>(len_left, writebuf.maxWriteSize());
            assert(len > 0);
            std::memcpy(writebuf.headPtr(), &r.bytes[src_i], len);
            writebuf.advanceHead(len);
            src_i += len;
            len_left -= len;
        }
        assert(len_left == 0);
        assert(src_i == r.len);
    }

    ssize_t len_left = packet.getBites().size();
    int src_i = 0;
    while (len_left > 0)
    {
        const size_t len = std::min<int>(len_left, writebuf.maxWriteSize());
        assert(len > 0);
        std::memcpy(writebuf.headPtr(), &packet.getBites()[src_i], len);
        writebuf.advanceHead(len);
        src_i += len;
        len_left -= len;
    }
    assert(len_left == 0);

    if (packet.packetType == PacketType::DISCONNECT)
        setReadyForDisconnect();

    setReadyForWriting(true);
}

// Helper method to avoid the exception ending up at the sender of messages, which would then get disconnected.
void Client::writeMqttPacketAndBlameThisClient(const MqttPacket &packet)
{
    try
    {
        this->writeMqttPacket(packet);
    }
    catch (std::exception &ex)
    {
        threadData->removeClient(fd);
    }
}

// Ping responses are always the same, so hardcoding it for optimization.
void Client::writePingResp()
{
    std::lock_guard<std::mutex> locker(writeBufMutex);

    if (2 > writebuf.freeSpace())
        writebuf.doubleSize();

    writebuf.headPtr()[0] = 0b11010000;
    writebuf.advanceHead(1);
    writebuf.headPtr()[0] = 0;
    writebuf.advanceHead(1);

    setReadyForWriting(true);
}

// SSL and non-SSL sockets behave differently. This wrapper unifies behavor for the caller.
ssize_t Client::writeWrap(int fd, const void *buf, size_t nbytes, IoWrapResult *error)
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
                logger->logf(LOG_DEBUG, "Write is incomplete: %d", err);
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

bool Client::writeBufIntoFd()
{
    std::unique_lock<std::mutex> lock(writeBufMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return true;

    // We can abort the write; the client is about to be removed anyway.
    if (disconnecting)
        return false;

    IoWrapResult error = IoWrapResult::Success;
    int n;
    while (writebuf.usedBytes() > 0 || incompleteSslWrite.hasPendingWrite())
    {
        n = writeWrap(fd, writebuf.tailPtr(), writebuf.maxReadSize(), &error);

        if (n > 0)
            writebuf.advanceTail(n);

        if (error == IoWrapResult::Interrupted)
            continue;
        if (error == IoWrapResult::Wouldblock)
            break;
    }

    const bool bufferHasData = writebuf.usedBytes() > 0;
    setReadyForWriting(bufferHasData || error == IoWrapResult::Wouldblock);

    if (!bufferHasData)
    {
        writeBufIsZeroCount++;
        bool doReset = (writeBufIsZeroCount >= 10 && writebuf.getSize() > (MAX_PACKET_SIZE / 10) && writebuf.bufferLastResizedSecondsAgo() > 30);
        doReset |= (writeBufIsZeroCount >= 100 && writebuf.bufferLastResizedSecondsAgo() > 300);

        if (doReset)
        {
            writeBufIsZeroCount = 0;
            writebuf.resetSize(CLIENT_BUFFER_SIZE);
        }
    }

    return true;
}

std::string Client::repr()
{
    std::ostringstream a;
    a << "[Client=" << clientid << ", user=" << username << ", fd=" << fd << "]";
    a.flush();
    return a.str();
}

bool Client::keepAliveExpired()
{
    if (!authenticated)
        return lastActivity + 20 < time(NULL);

    bool result = (lastActivity + (keepalive*10/5)) < time(NULL);
    return result;
}

std::string Client::getKeepAliveInfoString() const
{
    std::string s = "authenticated: " + std::to_string(authenticated) + ", keep-alive: " + std::to_string(keepalive) + "s, last activity "
            + std::to_string(time(NULL) - lastActivity) + " seconds ago.";
    return s;
}

// Call this from a place you know the writeBufMutex is locked, or we're still only doing SSL accept.
void Client::setReadyForWriting(bool val)
{
    if (disconnecting)
        return;

    if (sslReadWantsWrite)
        val = true;

    if (val == this->readyForWriting)
        return;

    readyForWriting = val;
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    if (readyForReading)
        ev.events |= EPOLLIN;
    if (readyForWriting)
        ev.events |= EPOLLOUT;
    check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_MOD, fd, &ev));
}

void Client::setReadyForReading(bool val)
{
    if (disconnecting)
        return;

    if (val == this->readyForReading)
        return;

    readyForReading = val;
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    if (readyForReading)
        ev.events |= EPOLLIN;
    if (readyForWriting)
        ev.events |= EPOLLOUT;
    check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_MOD, fd, &ev));
}

bool Client::bufferToMqttPackets(std::vector<MqttPacket> &packetQueueIn, Client_p &sender)
{
    while (readbuf.usedBytes() >= MQTT_HEADER_LENGH)
    {
        // Determine the packet length by decoding the variable length
        int remaining_length_i = 1; // index of 'remaining length' field is one after start.
        uint fixed_header_length = 1;
        int multiplier = 1;
        uint packet_length = 0;
        unsigned char encodedByte = 0;
        do
        {
            fixed_header_length++;

            // This happens when you only don't have all the bytes that specify the remaining length.
            if (fixed_header_length > readbuf.usedBytes())
                return false;

            encodedByte = readbuf.peakAhead(remaining_length_i++);
            packet_length += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            if (multiplier > 128*128*128*128)
                throw ProtocolError("Malformed Remaining Length.");
        }
        while ((encodedByte & 128) != 0);
        packet_length += fixed_header_length;

        if (!authenticated && packet_length >= 1024*1024)
        {
            throw ProtocolError("An unauthenticated client sends a packet of 1 MB or bigger? Probably it's just random bytes.");
        }

        if (packet_length <= readbuf.usedBytes())
        {
            MqttPacket packet(readbuf, packet_length, fixed_header_length, sender);
            packetQueueIn.push_back(std::move(packet));
        }
        else
            break;
    }

    setReadyForReading(readbuf.freeSpace() > 0);

    if (readbuf.usedBytes() == 0)
    {
        readBufIsZeroCount++;
        bool doReset = (readBufIsZeroCount >= 10 && readbuf.getSize() > (MAX_PACKET_SIZE / 10) && readbuf.bufferLastResizedSecondsAgo() > 30);
        doReset |= (readBufIsZeroCount >= 100 && readbuf.bufferLastResizedSecondsAgo() > 300);

        if (doReset)
        {
            readBufIsZeroCount = 0;
            readbuf.resetSize(CLIENT_BUFFER_SIZE);
        }
    }

    return true;
}

void Client::setClientProperties(const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive, bool cleanSession)
{
    this->clientid = clientId;
    this->username = username;
    this->connectPacketSeen = connectPacketSeen;
    this->keepalive = keepalive;
    this->cleanSession = cleanSession;
}

void Client::setWill(const std::string &topic, const std::string &payload, bool retain, char qos)
{
    this->will_topic = topic;
    this->will_payload = payload;
    this->will_retain = retain;
    this->will_qos = qos;
}

void Client::assignSession(std::shared_ptr<Session> &session)
{
    this->session = session;
}

std::shared_ptr<Session> Client::getSession()
{
    return this->session;
}

void Client::setDisconnectReason(const std::string &reason)
{
    // If we have a chain of errors causing this to be set, probably the first one is the most interesting.
    if (!disconnectReason.empty())
        return;

    this->disconnectReason = reason;
}

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

bool IncompleteSslWrite::hasPendingWrite()
{
    return buf != nullptr;
}
