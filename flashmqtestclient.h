#ifndef FLASHMQTESTCLIENT_H
#define FLASHMQTESTCLIENT_H

#include <thread>
#include <memory>

#include "subscriptionstore.h"

/**
 * @brief The FlashMQTestClient class uses the existing server code as a client, for testing purposes.
 */
class FlashMQTestClient
{
    std::shared_ptr<Settings> settings;
    std::shared_ptr<ThreadData> testServerWorkerThreadData;
    std::shared_ptr<Client> client;

    std::shared_ptr<ThreadData> dummyThreadData;

    std::mutex receivedListMutex;

    static int clientCount;

    void waitForCondition(std::function<bool()> f);


public:
    std::list<MqttPacket> receivedPackets;
    std::list<MqttPacket> receivedPublishes;

    FlashMQTestClient();
    ~FlashMQTestClient();

    void start();
    void connectClient(ProtocolVersion protocolVersion);
    void subscribe(const std::string topic, char qos);
    void publish(const std::string &topic, const std::string &payload, char qos);
    void clearReceivedLists();

    void waitForQuit();
    void waitForConnack();
    void waitForMessageCount(const size_t count);
};

#endif // FLASHMQTESTCLIENT_H
