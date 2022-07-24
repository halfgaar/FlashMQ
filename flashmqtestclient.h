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
    std::shared_ptr<WillPublish> will;

    std::shared_ptr<ThreadData> dummyThreadData;

    std::mutex receivedListMutex;

    static int clientCount;

    void waitForCondition(std::function<bool()> f, int timeout = 1);


public:
    std::vector<MqttPacket> receivedPackets;
    std::vector<MqttPacket> receivedPublishes;

    FlashMQTestClient();
    ~FlashMQTestClient();

    void start();
    void connectClient(ProtocolVersion protocolVersion);
    void connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval);
    void connectClient(ProtocolVersion protocolVersion, bool clean_start, uint32_t session_expiry_interval, std::function<void(Connect&)> manipulateConnect);
    void subscribe(const std::string topic, char qos);
    void publish(const std::string &topic, const std::string &payload, char qos);
    void publish(Publish &pub);
    void clearReceivedLists();
    void setWill(std::shared_ptr<WillPublish> &will);
    void disconnect(ReasonCodes reason);

    void waitForQuit();
    void waitForConnack();
    void waitForMessageCount(const size_t count, int timeout = 1);
};

#endif // FLASHMQTESTCLIENT_H
