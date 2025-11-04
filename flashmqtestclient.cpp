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
#include "threadglobals.h"

#define TEST_CLIENT_MAX_EVENTS 25

int FlashMQTestClient::clientCount = 0;

void FlashMQTestClient::ReceivedObjects::clear()
{
    receivedPackets.clear();
    receivedPublishes.clear();
}

FlashMQTestClient::FlashMQTestClient() :
    testServerWorkerThreadData(0, settings, pluginLoader)
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

        if (f())
            break;
    }

    if (!f())
    {
        throw std::runtime_error("Wait condition failed.");
    }
}

void FlashMQTestClient::clearReceivedLists()
{
    auto received_objects_locked = receivedObjects.lock();
    received_objects_locked->clear();
}

void FlashMQTestClient::setWill(std::shared_ptr<WillPublish> &will)
{
    this->will = will;
}

void FlashMQTestClient::disconnect(ReasonCodes reason)
{
    std::shared_ptr<Client> client = client_weak.lock();

    client->setDisconnectStage(DisconnectStage::SendPendingAppData);
    Disconnect d(client->getProtocolVersion(), reason);
    client->writeMqttPacket(d);
}

void FlashMQTestClient::start()
{
    testServerWorkerThreadData.start();
}

void FlashMQTestClient::connectClient(ProtocolVersion protocolVersion, int port, bool _waitForConnack)
{
    connectClient(protocolVersion, true, 0, [](Connect&){}, port, _waitForConnack);
}

void FlashMQTestClient::connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval, int port, bool _waitForConnack)
{
    connectClient(protocolVersion, clean_start, session_expiry_interval, [](Connect&){}, port, _waitForConnack);
}

void FlashMQTestClient::connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval,
                                      std::function<void(Connect&)> manipulateConnect, int port, bool _waitForConnack)
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

    std::shared_ptr<Client> client = std::make_shared<Client>(
        ClientType::Normal, sockfd, testServerWorkerThreadData.getThreadData(), FmqSsl(), ConnectionProtocol::Mqtt, false, reinterpret_cast<struct sockaddr*>(&servaddr), settings);
    this->client_weak = client;
    client->setClientProperties(protocolVersion, clientid, {}, "user", false, 60);

    {
        // Hack to make it work with the rvalue argument whilest not voiding our own client.
        std::shared_ptr<Client> dummyToMoveFrom = client;
        client->addToEpoll(EPOLLIN); // Normally the worker thread does this, but we must avoid races, for the code below, that already tries to mod the epoll.
        testServerWorkerThreadData->giveClient(std::move(dummyToMoveFrom));
    }

    // This gets called in the test client's worker thread, but the STL container's minimal thread safety should be enough: only list manipulation is
    // mutexed, elements within are not.
    client->onPacketReceived = [this](MqttPacket &pack)
    {
        std::shared_ptr<Client> client = this->client_weak.lock();

        if (pack.packetType == PacketType::PUBLISH)
        {
            pack.parsePublishData(client);

            auto received_objects_locked = receivedObjects.lock();

            MqttPacket copyPacket = pack;
            received_objects_locked->receivedPublishes.push_back(copyPacket);

            if (pack.getPublishData().qos == 1)
            {
                PubResponse pubAck(client->getProtocolVersion(), PacketType::PUBACK, ReasonCodes::Success, pack.getPacketId());
                client->writeMqttPacketAndBlameThisClient(pubAck);
            }
            else if (pack.getPublishData().qos == 2)
            {
                PubResponse pubAck(client->getProtocolVersion(), PacketType::PUBREC, ReasonCodes::Success, pack.getPacketId());
                client->writeMqttPacketAndBlameThisClient(pubAck);
            }
        }
        else if (pack.packetType == PacketType::PUBREL)
        {
            pack.parsePubRelData();
            PubResponse pubComp(client->getProtocolVersion(), PacketType::PUBCOMP, ReasonCodes::Success, pack.getPacketId());
            client->writeMqttPacketAndBlameThisClient(pubComp);
        }
        else if (pack.packetType == PacketType::PUBREC)
        {
            pack.parsePubRecData();
            PubResponse pubRel(client->getProtocolVersion(), PacketType::PUBREL, ReasonCodes::Success, pack.getPacketId());
            client->writeMqttPacketAndBlameThisClient(pubRel);
        }

        auto received_objects_locked = receivedObjects.lock();
        received_objects_locked->receivedPackets.push_back(std::move(pack));
    };

    Connect connect(protocolVersion, client->getClientId());
    connect.will = this->will;
    connect.clean_start = clean_start;
    connect.sessionExpiryInterval = session_expiry_interval;

    manipulateConnect(connect);

    MqttPacket connectPack(connect);
    client->writeMqttPacketAndBlameThisClient(connectPack);

    if (_waitForConnack)
    {
        waitForConnack();
        client->setAuthenticated(true);
    }
}

void FlashMQTestClient::subscribe(const std::string topic, uint8_t qos, bool noLocal, bool retainAsPublished, uint32_t subscriptionIdentifier, RetainHandling retainHandling)
{
    std::shared_ptr<Client> client = client_weak.lock();

    clearReceivedLists();

    const uint16_t packet_id = 66;

    std::vector<Subscribe> subs;
    subs.emplace_back(topic, qos);
    subs.back().noLocal = noLocal;
    subs.back().retainAsPublished = retainAsPublished;
    subs.back().retainHandling = retainHandling;
    MqttPacket subPack(client->getProtocolVersion(), packet_id, subscriptionIdentifier, subs);
    client->writeMqttPacketAndBlameThisClient(subPack);

    waitForCondition([&]() {
        auto ro = receivedObjects.lock();
        return !ro->receivedPackets.empty() && ro->receivedPackets.front().packetType == PacketType::SUBACK;
    });

    auto ro = receivedObjects.lock();

    MqttPacket &subAck = ro->receivedPackets.front();
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
    std::shared_ptr<Client> client = client_weak.lock();

    clearReceivedLists();

    const uint16_t packet_id = 66;

    Unsubscribe unsub(client->getProtocolVersion(), packet_id, topic);
    MqttPacket unsubPack(unsub);
    client->writeMqttPacketAndBlameThisClient(unsubPack);

    waitForCondition([&]() {
        auto ro = receivedObjects.lock();
        return  !ro->receivedPackets.empty() && ro->receivedPackets.front().packetType == PacketType::UNSUBACK;
    });

    // TODO: parse the UNSUBACK and check reason codes.
}

void FlashMQTestClient::publish(Publish &pub)
{
    std::shared_ptr<Client> client = client_weak.lock();

    clearReceivedLists();

    const uint16_t packet_id = 77;

    MqttPacket pubPack(client->getProtocolVersion(), pub);
    if (pub.qos > 0)
        pubPack.setPacketId(packet_id);
    client->writeMqttPacketAndBlameThisClient(pubPack);

    if (pub.qos == 1)
    {
        waitForCondition([&]() {
            auto ro = receivedObjects.lock();
            return !ro->receivedPackets.empty();
        });

        auto ro = receivedObjects.lock();

        MqttPacket &pubAckPack = ro->receivedPackets.front();
        pubAckPack.parsePubAckData();

        if (pubAckPack.packetType != PacketType::PUBACK)
            throw std::runtime_error("First packet received from server is not a PUBACK, but " + packetTypeToString(pubAckPack.packetType));

        if (pubAckPack.getPacketId() != packet_id)
            throw std::runtime_error("Packet ID mismatch between publish and ack on QoS 1 publish.");

        // We may have received publishes along with our acks, if we publish and subscribe to the same topic with
        // one client, so we have to filter out the publishes.
        int metaPacketCount = std::count_if(ro->receivedPackets.begin(), ro->receivedPackets.end(), [](MqttPacket &pack){return pack.packetType != PacketType::PUBLISH;});

        if (metaPacketCount != 1)
            throw std::runtime_error("Packet ID mismatch on QoS 1 publish or packet count wrong.");
    }
    else if (pub.qos == 2)
    {
        waitForCondition([&]() {
            auto ro = receivedObjects.lock();
            return ro->receivedPackets.size() >= 2;
        });

        auto ro = receivedObjects.lock();

        MqttPacket &pubRecPack = ro->receivedPackets.front();
        pubRecPack.parsePubRecData();
        MqttPacket &pubCompPack = ro->receivedPackets.back();
        pubCompPack.parsePubComp();

        if (pubRecPack.packetType != PacketType::PUBREC)
            throw std::runtime_error("First packet received from server is not a PUBREC, but " + packetTypeToString(pubRecPack.packetType));

        if (pubCompPack.packetType != PacketType::PUBCOMP)
            throw std::runtime_error("Last packet received from server is not a PUBCOMP.");

        if (pubRecPack.getPacketId() != packet_id || pubCompPack.getPacketId() != packet_id)
            throw std::runtime_error("Packet ID mismatch on QoS 2 publish.");
    }
}

void FlashMQTestClient::writeAuth(const Auth &auth)
{
    std::shared_ptr<Client> client = client_weak.lock();

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
    testServerWorkerThreadData.waitForQuit();
}

void FlashMQTestClient::waitForConnack()
{
    waitForCondition([&]() {
        auto ro = receivedObjects.lock();

        return std::any_of(ro->receivedPackets.begin(), ro->receivedPackets.end(), [](const MqttPacket &p) {
            return p.packetType == PacketType::CONNACK || p.packetType == PacketType::AUTH;
        });
    });
}

void FlashMQTestClient::waitForDisconnectPacket()
{
    waitForCondition([&]() {
        auto ro = receivedObjects.lock();

        return std::any_of(ro->receivedPackets.begin(), ro->receivedPackets.end(), [](const MqttPacket &p) {
            return p.packetType == PacketType::DISCONNECT;
        });
    });
}

void FlashMQTestClient::waitForMessageCount(const size_t count, int timeout)
{
    waitForCondition([&]() {
        auto ro = receivedObjects.lock();

        return ro->receivedPublishes.size() >= count;
    }, timeout);
}

void FlashMQTestClient::waitForPacketCount(const size_t count, int timeout)
{
    waitForCondition([&]() {
        auto ro = receivedObjects.lock();
        return ro->receivedPackets.size() >= count;
    }, timeout);
}

std::shared_ptr<Client> FlashMQTestClient::getClient()
{
    std::shared_ptr<Client> client = client_weak.lock();
    return client;
}

std::string FlashMQTestClient::getClientId()
{
    std::shared_ptr<Client> client = client_weak.lock();
    return client->getClientId();
}

ProtocolVersion FlashMQTestClient::getProtocolVersion()
{
    std::shared_ptr<Client> client = client_weak.lock();
    return client->getProtocolVersion();
}

bool FlashMQTestClient::clientExpired() const
{
    return client_weak.expired();
}
