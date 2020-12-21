#include "client.h"

#include <cstring>
#include <sstream>
#include <iostream>
#include <cassert>

Client::Client(int fd, ThreadData_p threadData) :
    fd(fd),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    readbuf = (char*)malloc(CLIENT_BUFFER_SIZE);
    writebuf = (char*)malloc(CLIENT_BUFFER_SIZE);

    if (readbuf == NULL || writebuf == NULL)
        throw std::runtime_error("Malloc error constructing client.");
}

Client::~Client()
{
    closeConnection();
    free(readbuf);
    free(writebuf);
}

void Client::closeConnection()
{
    if (fd < 0)
        return;
    check<std::runtime_error>(epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL));
    close(fd);
    fd = -1;
}

// false means any kind of error we want to get rid of the client for.
bool Client::readFdIntoBuffer()
{
    if (wi > CLIENT_MAX_BUFFER_SIZE)
    {
        setReadyForReading(false);
        return true;
    }

    int n;
    while ((n = read(fd, &readbuf[wi], getReadBufMaxWriteSize())) != 0)
    {
        if (n > 0)
        {
            wi += n;

            if (getReadBufMaxWriteSize() == 0)
            {
                growReadBuffer();
            }
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
    }

    if (n == 0) // client disconnected.
    {
        std::cerr << "normal disconnect" << std::endl;
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
    if (!writeBufMutex.try_lock())
        return true;

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

    writeBufMutex.unlock();
    return true;
}

std::string Client::repr()
{
    std::ostringstream a;
    a << "Client = " << clientid << ", user = " << username;
    a.flush();
    return a.str();
}

void Client::setReadyForWriting(bool val)
{
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
    while (getReadBufBytesUsed() >= MQTT_HEADER_LENGH)
    {
        // Determine the packet length by decoding the variable length
        size_t remaining_length_i = 1;
        int multiplier = 1;
        size_t packet_length = 0;
        unsigned char encodedByte = 0;
        do
        {
            if (remaining_length_i >= getReadBufBytesUsed())
                break;
            encodedByte = readbuf[ri + remaining_length_i++];
            packet_length += (encodedByte & 127) * multiplier;
            multiplier *= 128;
            if (multiplier > 128*128*128)
                return false;
        }
        while ((encodedByte & 128) != 0);
        packet_length += remaining_length_i;

        if (!authenticated && packet_length >= 1024*1024)
        {
            throw ProtocolError("An unauthenticated client sends a packet of 1 MB or bigger? Probably it's just random bytes.");
        }

        if (packet_length <= getReadBufBytesUsed())
        {
            MqttPacket packet(&readbuf[ri], packet_length, remaining_length_i, sender);
            packetQueueIn.push_back(std::move(packet));

            ri += packet_length;
            assert(ri <= wi);
        }
        else
            break;

    }

    if (ri == wi)
    {
        ri = 0;
        wi = 0;
        setReadyForReading(true);
    }

    return true;

    // TODO: reset buffer to normal size after a while of not needing it, or not needing the extra space.
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















