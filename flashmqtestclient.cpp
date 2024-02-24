/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "flashmqtestclient.h"

#include <sys/epoll.h>
#include <cstring>
#include <errno.h>
#include <functional>

#include "threadloop.h"
#include "utils.h"
#include "client.h"
#include "threaddata.h"

#define TEST_CLIENT_MAX_EVENTS 25

int FlashMQTestClient::clientCount = 0;

FlashMQTestClient::FlashMQTestClient() :
    testServerWorkerThreadData(std::make_shared<ThreadData>(0, settings, pluginLoader)),
    dummyThreadData(std::make_shared<ThreadData>(666, settings, pluginLoader))
{

}

/**
 * @brief FlashMQTestClient::~FlashMQTestClient properly quits the threads when exiting.
 *
 * This prevents accidental crashes on calling terminate(), and Qt Macro's prematurely end the method, skipping explicit waits after the tests.
 */
FlashMQTestClient::~FlashMQTestClient()
{
    waitForQuit();
}

void FlashMQTestClient::waitForCondition(std::function<bool()> f, int timeout)
{
    const int loopCount = (timeout * 1000) / 10;

    int n = 0;
    while(n++ < loopCount)
    {
        usleep(10000);

        std::lock_guard<std::mutex> locker(receivedListMutex);

        if (f())
            break;
    }

    std::lock_guard<std::mutex> locker(receivedListMutex);

    if (!f())
    {
        throw std::runtime_error("Wait condition failed.");
    }
}

void FlashMQTestClient::clearReceivedLists()
{
    std::lock_guard<std::mutex> locker(receivedListMutex);
    receivedPackets.clear();
    receivedPublishes.clear();
}

void FlashMQTestClient::setWill(std::shared_ptr<WillPublish> &will)
{
    this->will = will;
}

void FlashMQTestClient::disconnect(ReasonCodes reason)
{
    client->setReadyForDisconnect();
    Disconnect d(this->client->getProtocolVersion(), reason);
    client->writeMqttPacket(d);
}

void FlashMQTestClient::start()
{
    testServerWorkerThreadData->start(&do_thread_work);
}

void FlashMQTestClient::connectClient(ProtocolVersion protocolVersion, int port)
{
    connectClient(protocolVersion, true, 0, [](Connect&){}, port);
}

void FlashMQTestClient::connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval, int port)
{
    connectClient(protocolVersion, clean_start, session_expiry_interval, [](Connect&){}, port);
}

void FlashMQTestClient::connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval,
                                      std::function<void(Connect&)> manipulateConnect, int port)
{
    int sockfd = check<std::runtime_error>(socket(AF_INET, SOCK_STREAM, 0));

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));

    const std::string hostname = "127.0.0.1";

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(hostname.c_str());
    servaddr.sin_port = htons(port);

    int flags = fcntl(sockfd, F_GETFL);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(sockfd, reinterpret_cast<struct sockaddr*>(&servaddr), sizeof (servaddr));

    if (rc < 0 && errno != EINPROGRESS)
    {
        throw std::runtime_error(strerror(errno));
    }

    const std::string clientid = formatString("testclient_%d", clientCount++);

    this->client = std::make_shared<Client>(sockfd, testServerWorkerThreadData, nullptr, false, false, reinterpret_cast<struct sockaddr*>(&servaddr), settings);
    this->client->setClientProperties(protocolVersion, clientid, "user", false, 60);

    {
        // Hack to make it work with the rvalue argument. Because our test client retains ownership of 'client', we can get away
        // with this. See git what caused this change.
        std::shared_ptr<Client> dummyToMoveFrom = this->client;
        testServerWorkerThreadData->giveClient(std::move(dummyToMoveFrom));
    }

    // This gets called in the test client's worker thread, but the STL container's minimal thread safety should be enough: only list manipulation is
    // mutexed, elements within are not.
    client->onPacketReceived = [&](MqttPacket &pack)
    {
        std::lock_guard<std::mutex> locker(receivedListMutex);

        if (pack.packetType == PacketType::PUBLISH)
        {
            pack.parsePublishData();

            MqttPacket copyPacket = pack;
            this->receivedPublishes.push_back(copyPacket);

            if (pack.getPublishData().qos > 0)
            {
                PubResponse pubAck(this->client->getProtocolVersion(), PacketType::PUBACK, ReasonCodes::Success, pack.getPacketId());
                this->client->writeMqttPacketAndBlameThisClient(pubAck);
            }
        }
        else if (pack.packetType == PacketType::PUBREL)
        {
            pack.parsePubRelData();
            PubResponse pubComp(this->client->getProtocolVersion(), PacketType::PUBCOMP, ReasonCodes::Success, pack.getPacketId());
            this->client->writeMqttPacketAndBlameThisClient(pubComp);
        }
        else if (pack.packetType == PacketType::PUBREC)
        {
            pack.parsePubRecData();
            PubResponse pubRel(this->client->getProtocolVersion(), PacketType::PUBREL, ReasonCodes::Success, pack.getPacketId());
            this->client->writeMqttPacketAndBlameThisClient(pubRel);
        }

        this->receivedPackets.push_back(std::move(pack));
    };

    Connect connect(protocolVersion, client->getClientId());
    connect.will = this->will;
    connect.clean_start = clean_start;
    connect.constructPropertyBuilder();
    connect.propertyBuilder->writeSessionExpiry(session_expiry_interval);

    manipulateConnect(connect);

    MqttPacket connectPack(connect);
    this->client->writeMqttPacketAndBlameThisClient(connectPack);

    waitForConnack();
    this->client->setAuthenticated(true);
}

void FlashMQTestClient::subscribe(const std::string topic, uint8_t qos, bool noLocal, bool retainAsPublished)
{
    clearReceivedLists();

    const uint16_t packet_id = 66;

    Subscribe sub(client->getProtocolVersion(), packet_id, topic, qos);
    sub.noLocal = noLocal;
    sub.retainAsPublished = retainAsPublished;
    MqttPacket subPack(sub);
    client->writeMqttPacketAndBlameThisClient(subPack);

    waitForCondition([&]() {
        return  !this->receivedPackets.empty() && this->receivedPackets.front().packetType == PacketType::SUBACK;
    });

    MqttPacket &subAck = this->receivedPackets.front();
    SubAckData data = subAck.parseSubAckData();

    if (data.packet_id != packet_id)
        throw std::runtime_error("Incorrect packet id in suback");

    if (!std::all_of(data.subAckCodes.begin(), data.subAckCodes.end(), [&](uint8_t x) { return x <= qos ;}))
    {
        throw SubAckIsError("Suback indicates error.");
    }
}

void FlashMQTestClient::unsubscribe(const std::string &topic)
{
    clearReceivedLists();

    const uint16_t packet_id = 66;

    Unsubscribe unsub(client->getProtocolVersion(), packet_id, topic);
    MqttPacket unsubPack(unsub);
    client->writeMqttPacketAndBlameThisClient(unsubPack);

    waitForCondition([&]() {
        return  !this->receivedPackets.empty() && this->receivedPackets.front().packetType == PacketType::UNSUBACK;
    });

    // TODO: parse the UNSUBACK and check reason codes.
}

void FlashMQTestClient::publish(Publish &pub)
{
    clearReceivedLists();

    const uint16_t packet_id = 77;

    MqttPacket pubPack(client->getProtocolVersion(), pub);
    if (pub.qos > 0)
        pubPack.setPacketId(packet_id);
    client->writeMqttPacketAndBlameThisClient(pubPack);

    if (pub.qos == 1)
    {
        waitForCondition([&]() {
           return !this->receivedPackets.empty();
        });

        MqttPacket &pubAckPack = this->receivedPackets.front();
        pubAckPack.parsePubAckData();

        if (pubAckPack.packetType != PacketType::PUBACK)
            throw std::runtime_error("First packet received from server is not a PUBACK.");

        if (pubAckPack.getPacketId() != packet_id)
            throw std::runtime_error("Packet ID mismatch between publish and ack on QoS 1 publish.");

        // We may have received publishes along with our acks, if we publish and subscribe to the same topic with
        // one client, so we have to filter out the publishes.
        int metaPacketCount = std::count_if(receivedPackets.begin(), receivedPackets.end(), [](MqttPacket &pack){return pack.packetType != PacketType::PUBLISH;});

        if (metaPacketCount != 1)
            throw std::runtime_error("Packet ID mismatch on QoS 1 publish or packet count wrong.");
    }
    else if (pub.qos == 2)
    {
        waitForCondition([&]() {
           return this->receivedPackets.size() >= 2;
        });

        MqttPacket &pubRecPack = this->receivedPackets.front();
        pubRecPack.parsePubRecData();
        MqttPacket &pubCompPack = this->receivedPackets.back();
        pubCompPack.parsePubComp();

        if (pubRecPack.packetType != PacketType::PUBREC)
            throw std::runtime_error("First packet received from server is not a PUBREC.");

        if (pubCompPack.packetType != PacketType::PUBCOMP)
            throw std::runtime_error("Last packet received from server is not a PUBCOMP.");

        if (pubRecPack.getPacketId() != packet_id || pubCompPack.getPacketId() != packet_id)
            throw std::runtime_error("Packet ID mismatch on QoS 2 publish.");
    }
}

void FlashMQTestClient::writeAuth(const Auth &auth)
{
    MqttPacket pack(auth);
    client->writeMqttPacketAndBlameThisClient(pack);
}

void FlashMQTestClient::publish(const std::string &topic, const std::string &payload, uint8_t qos)
{
    Publish pub(topic, payload, qos);
    publish(pub);
}

void FlashMQTestClient::waitForQuit()
{
    testServerWorkerThreadData->queueQuit();
    testServerWorkerThreadData->waitForQuit();
}

void FlashMQTestClient::waitForConnack()
{
    waitForCondition([&]() {
        return std::any_of(this->receivedPackets.begin(), this->receivedPackets.end(), [](const MqttPacket &p) {
            return p.packetType == PacketType::CONNACK || p.packetType == PacketType::AUTH;
        });
    });
}

void FlashMQTestClient::waitForDisconnectPacket()
{
    waitForCondition([&]() {
        return std::any_of(this->receivedPackets.begin(), this->receivedPackets.end(), [](const MqttPacket &p) {
            return p.packetType == PacketType::DISCONNECT;
        });
    });
}

void FlashMQTestClient::waitForMessageCount(const size_t count, int timeout)
{
    waitForCondition([&]() {
        return this->receivedPublishes.size() >= count;
    }, timeout);
}

void FlashMQTestClient::waitForPacketCount(const size_t count, int timeout)
{
    waitForCondition([&]() {
        return this->receivedPackets.size() >= count;
    }, timeout);
}

std::shared_ptr<Client> &FlashMQTestClient::getClient()
{
    return this->client;
}
