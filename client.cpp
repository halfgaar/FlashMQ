#include "client.h"

#include <cstring>
#include <sstream>
#include <iostream>
#include <cassert>

Client::Client(int fd, ThreadData_p threadData) :
    fd(fd),
    readbuf(CLIENT_BUFFER_SIZE),
    writebuf(CLIENT_BUFFER_SIZE),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Client::~Client()
{
    close(fd);
}

// Do this from a place you'll know ownwership of the shared_ptr is being given up everywhere, so the close happens when the last owner gives it up.
void Client::markAsDisconnecting()
{
    if (disconnecting)
        return;

    disconnecting = true;
    check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL));
}

// false means any kind of error we want to get rid of the client for.
bool Client::readFdIntoBuffer()
{
    if (disconnecting)
        return false;

    int n = 0;
    while (readbuf.freeSpace() > 0 && (n = read(fd, readbuf.headPtr(), readbuf.maxWriteSize())) != 0)
    {
        if (n > 0)
        {
            readbuf.advanceHead(n);
        }

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                check<std::runtime_error>(n);
        }

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

    if (n == 0) // client disconnected.
    {
        return false;
    }

    return true;
}

void Client::writeMqttPacket(const MqttPacket &packet)
{
    std::lock_guard<std::mutex> locker(writeBufMutex);

    // Grow as far as we can. We have to make room for one MQTT packet.
    while (packet.getSizeIncludingNonPresentHeader() > writebuf.freeSpace() && writebuf.getSize() < MAX_PACKET_SIZE)
    {
        writebuf.doubleSize();
    }

    // And drop a publish when it doesn't fit, even after resizing. This means we do allow pings.
    // TODO: when QoS is implemented, different filtering may be required.
    if (packet.packetType == PacketType::PUBLISH && packet.getSizeIncludingNonPresentHeader() > writebuf.freeSpace())
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

    std::cout << "Sending ping response to " << repr() << std::endl;

    if (2 > writebuf.freeSpace())
        writebuf.doubleSize();

    writebuf.headPtr()[0] = 0b11010000;
    writebuf.advanceHead(1);
    writebuf.headPtr()[0] = 0;
    writebuf.advanceHead(1);

    setReadyForWriting(true);
}

bool Client::writeBufIntoFd()
{
    std::unique_lock<std::mutex> lock(writeBufMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return true;

    // We can abort the write; the client is about to be removed anyway.
    if (disconnecting)
        return false;

    int n;
    while ((n = write(fd, writebuf.tailPtr(), writebuf.maxReadSize())) != 0)
    {
        if (n > 0)
            writebuf.advanceTail(n);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                check<std::runtime_error>(n);
        }
    }

    const bool bufferHasData = writebuf.usedBytes() > 0;
    setReadyForWriting(bufferHasData);

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

void Client::setReadyForWriting(bool val)
{
    if (disconnecting)
        return;

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

void Client::setClientProperties(const std::string &clientId, const std::string username, bool connectPacketSeen, uint16_t keepalive)
{
    this->clientid = clientId;
    this->username = username;
    this->connectPacketSeen = connectPacketSeen;
    this->keepalive = keepalive;
}

void Client::setWill(const std::string &topic, const std::string &payload, bool retain, char qos)
{
    this->will_topic = topic;
    this->will_payload = payload;
    this->will_retain = retain;
    this->will_qos = qos;
}















