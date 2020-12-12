#include "client.h"

#include <cstring>
#include <sstream>
#include <iostream>

Client::Client(int fd, ThreadData_p threadData) :
    fd(fd),
    threadData(threadData)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    readbuf = (char*)malloc(CLIENT_BUFFER_SIZE);
    writebuf = (char*)malloc(CLIENT_BUFFER_SIZE);
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
    epoll_ctl(threadData->epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    fd = -1;
}

// false means any kind of error we want to get rid of the client for.
bool Client::readFdIntoBuffer()
{
    int n;
    while ((n = read(fd, &readbuf[wi], getReadBufMaxWriteSize())) != 0)
    {
        if (n > 0)
        {
            wi += n;

            if (getReadBufBytesUsed() >= readBufsize)
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
                return false;
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
    if (packet.packetType == PacketType::PUBLISH && getWriteBufBytesUsed() > CLIENT_MAX_BUFFER_SIZE)
        return;

    if (packet.getSize() > getWriteBufMaxWriteSize())
        growWriteBuffer(packet.getSize());

    std::memcpy(&writebuf[wwi], &packet.getBites()[0], packet.getSize());
    wwi += packet.getSize();

    setReadyForWriting(true);
}

// Not sure if this is the method I want to use
void Client::writeMqttPacketLocked(const MqttPacket &packet)
{
    std::lock_guard<std::mutex> lock(writeBufMutex);
    writeMqttPacket(packet);
}

// Ping responses are always the same, so hardcoding it for optimization.
void Client::writePingResp()
{
    std::cout << "Sending ping response to " << repr() << std::endl;

    if (2 > getWriteBufMaxWriteSize())
        growWriteBuffer(CLIENT_BUFFER_SIZE);

    writebuf[wwi++] = 0b11010000;
    writebuf[wwi++] = 0;

    setReadyForWriting(true);
}

bool Client::writeBufIntoFd()
{
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
                return false;
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
    a << "Client = " << clientid << ", user = " << username;
    return a.str();
}

void Client::queueMessage(const MqttPacket &packet)
{


    // TODO: semaphores on stl containers?
}

void Client::queuedMessagesToBuffer()
{

}

void Client::setReadyForWriting(bool val)
{
    if (val == this->readyForWriting)
        return;

    readyForWriting = val;
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    if (val)
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

            if (ri > wi)
                throw std::runtime_error("hier");
        }
        else
            break;

    }

    if (ri == wi)
    {
        ri = 0;
        wi = 0;
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















