#include "client.h"

#include <cstring>
#include <sstream>
#include <iostream>
#include <cassert>

Client::Client(int fd, ThreadData_p threadData) :
    fd(fd),
    readbuf(CLIENT_BUFFER_SIZE),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    char *writebuf = (char*)malloc(CLIENT_BUFFER_SIZE);

    if (writebuf == NULL)
    {
        throw std::runtime_error("Malloc error constructing client.");
    }

    this->writebuf = writebuf;
}

Client::~Client()
{
    close(fd);
    free(writebuf);
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
            if (readbuf.getSize() * 2 < CLIENT_MAX_BUFFER_SIZE)
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

    if (packet.packetType == PacketType::PUBLISH && wwi > CLIENT_MAX_BUFFER_SIZE)
        return;

    if (packet.getSizeIncludingNonPresentHeader() > getWriteBufMaxWriteSize())
        growWriteBuffer(packet.getSizeIncludingNonPresentHeader());

    if (!packet.containsFixedHeader())
    {
        writebuf[wwi++] = packet.getFirstByte();
        RemainingLength r = packet.getRemainingLength();
        std::memcpy(&writebuf[wwi], r.bytes, r.len);
        wwi += r.len;
    }

    std::memcpy(&writebuf[wwi], &packet.getBites()[0], packet.getBites().size());
    wwi += packet.getBites().size();

    assert(wwi >= static_cast<int>(packet.getSizeIncludingNonPresentHeader()));
    assert(wwi <= static_cast<int>(writeBufsize));

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

    if (2 > getWriteBufMaxWriteSize())
        growWriteBuffer(CLIENT_BUFFER_SIZE);

    writebuf[wwi++] = 0b11010000;
    writebuf[wwi++] = 0;

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
    while ((n = write(fd, &writebuf[wri], getWriteBufBytesUsed())) != 0)
    {
        if (n > 0)
            wri += n;
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

    if (wri == wwi)
    {
        wri = 0;
        wwi = 0;

        setReadyForWriting(false);
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

void Client::growWriteBuffer(size_t add_size)
{
    if (add_size == 0)
        return;

    const size_t grow_by = std::max<size_t>(add_size, writeBufsize*2);
    const size_t newBufSize = writeBufsize + grow_by;
    char *writebuf = (char*)realloc(this->writebuf, newBufSize);

    if (writebuf == NULL)
        throw std::runtime_error("Memory allocation failure in growWriteBuffer()");

    this->writebuf = writebuf;
    writeBufsize = newBufSize;

    std::cout << "New write buf size: " << writeBufsize << std::endl;
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
        int fixed_header_length = 1;
        int multiplier = 1;
        int packet_length = 0;
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
            if (multiplier > 128*128*128)
                return false;
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
        // TODO: reset buffer to normal size after a while of not needing it, or not needing the extra space.
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















