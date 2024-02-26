#include "maintests.h"
#include "testhelpers.h"
#include "flashmqtestclient.h"
#include "conffiletemp.h"

void MainTests::testMqtt3will()
{
    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will";
    will->payload = "mypayload";
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt311);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311);
    receiver.subscribe("my/will", 0);

    sender.reset();

    receiver.waitForMessageCount(1);

    MqttPacket pubPack = receiver.receivedPublishes.front();
    pubPack.parsePublishData();

    QCOMPARE(pubPack.getPublishData().topic, "my/will");
    QCOMPARE(pubPack.getPublishData().payload, "mypayload");
    QCOMPARE(pubPack.getPublishData().qos, 0);
}

void MainTests::testMqtt3NoWillOnDisconnect()
{
    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will/testMqtt3NoWillOnDisconnect";
    will->payload = "mypayload";
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt311);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311);
    receiver.subscribe("my/will/testMqtt3NoWillOnDisconnect", 0);

    receiver.clearReceivedLists();

    sender->disconnect(ReasonCodes::Success);
    sender.reset();

    usleep(250000);

    QVERIFY(receiver.receivedPackets.empty());
}

void MainTests::testMqtt5NoWillOnDisconnect()
{
    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will/testMqtt5NoWillOnDisconnect";
    will->payload = "mypayload";
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("my/will/testMqtt3NoWillOnDisconnect", 0);

    receiver.clearReceivedLists();

    sender->disconnect(ReasonCodes::Success);
    sender.reset();

    usleep(250000);

    QVERIFY(receiver.receivedPackets.empty());
}

void MainTests::testMqtt5DelayedWill()
{
    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will/testMqtt5DelayedWill";
    will->payload = "mypayload";
    will->constructPropertyBuilder();
    will->propertyBuilder->writeWillDelay(2);
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt5, true, 60);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5, true, 60);
    receiver.subscribe("my/will/testMqtt5DelayedWill", 0);

    receiver.clearReceivedLists();

    sender.reset();

    usleep(250000);
    QVERIFY(receiver.receivedPackets.empty());

    receiver.waitForMessageCount(1, 5);

    MqttPacket pubPack = receiver.receivedPublishes.front();
    pubPack.parsePublishData();

    QCOMPARE(pubPack.getPublishData().topic, "my/will/testMqtt5DelayedWill");
    QCOMPARE(pubPack.getPublishData().payload, "mypayload");
    QCOMPARE(pubPack.getPublishData().qos, 0);
}

void MainTests::testMqtt5DelayedWillAlwaysOnSessionEnd()
{
    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will/testMqtt5DelayedWillAlwaysOnSessionEnd";
    will->payload = "mypayload";
    will->constructPropertyBuilder();
    will->propertyBuilder->writeWillDelay(120); // This long delay should not matter, because the session expires after 2s.
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt5, true, 2);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5, true, 60);
    receiver.subscribe("my/will/testMqtt5DelayedWillAlwaysOnSessionEnd", 0);

    receiver.clearReceivedLists();

    sender.reset();

    usleep(1000000);
    QVERIFY(receiver.receivedPackets.empty());

    receiver.waitForMessageCount(1, 2);

    MqttPacket pubPack = receiver.receivedPublishes.front();
    pubPack.parsePublishData();

    QCOMPARE(pubPack.getPublishData().topic, "my/will/testMqtt5DelayedWillAlwaysOnSessionEnd");
    QCOMPARE(pubPack.getPublishData().payload, "mypayload");
    QCOMPARE(pubPack.getPublishData().qos, 0);
}

/**
 * @brief MainTests::testWillOnSessionTakeOvers tests sending wills for both persistent and non-persistent sessions.
 *
 * Mosquitto is more liberal with not sending wills and will also not send one when you're taking over a persistent session. But, to me it seems
 * that the specs say you always send wills on client disconnects.
 *
 * See https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html "3.1.4 CONNECT Actions"
 *
 * See testOverrideWillDelayOnSessionDestructionByTakeOver() for more details.
 */
void MainTests::testWillOnSessionTakeOvers()
{
    std::list<bool> cleanStarts { false, true};

    for (bool cleanStart : cleanStarts)
    {
        FlashMQTestClient receiver;
        receiver.start();
        receiver.connectClient(ProtocolVersion::Mqtt311);
        receiver.subscribe("my/will", 0);

        FlashMQTestClient sender;
        sender.start();
        std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
        will->topic = "my/will";
        will->payload = "mypayload";
        sender.setWill(will);
        sender.connectClient(ProtocolVersion::Mqtt311, cleanStart, 0, [](Connect &connect){
            connect.clientid = "OneOfOne";
        });

        FlashMQTestClient sender2;
        sender2.start();
        std::shared_ptr<WillPublish> will2 = std::make_shared<WillPublish>();
        will2->topic = "my/will";
        will2->payload = "mypayload";
        sender2.setWill(will2);
        sender2.connectClient(ProtocolVersion::Mqtt311, cleanStart, 0, [](Connect &connect){
            connect.clientid = "OneOfOne";
        });

        receiver.waitForMessageCount(1);

        MqttPacket pubPack = receiver.receivedPublishes.front();
        pubPack.parsePublishData();

        QCOMPARE(pubPack.getPublishData().topic, "my/will");
        QCOMPARE(pubPack.getPublishData().payload, "mypayload");
        QCOMPARE(pubPack.getPublishData().qos, 0);
    }
}

/**
 * @brief MainTests::testOverrideWillDelayOnSessionDestructionByTakeOver tests that when you connect with a second 'clean start' client, the delayed
 * will of the session you're destroying is sent.
 *
 * Mosquitto is more liberal with not sending wills and will also not send one when you're taking over a persistent session. But, to me it seems
 * that the specs say you always send wills on client disconnects and actually hasten delayed wills when you kill the session containing the delayed will
 * by connecting a new session with the same ID using 'clean start'.
 *
 * See https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html "3.1.4 CONNECT Actions"
 */
void MainTests::testOverrideWillDelayOnSessionDestructionByTakeOver()
{
    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311);
    receiver.subscribe("my/will", 0);

    FlashMQTestClient sender;
    sender.start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will";
    will->payload = "mypayload";
    will->will_delay = 120;
    sender.setWill(will);
    sender.connectClient(ProtocolVersion::Mqtt311, false, 300, [](Connect &connect){
        connect.clientid = "OneOfOne";
    });

    FlashMQTestClient sender2;
    sender2.start();
    sender2.connectClient(ProtocolVersion::Mqtt311, true, 300, [](Connect &connect){
        connect.clientid = "OneOfOne";
    });

    receiver.waitForMessageCount(1);

    MqttPacket pubPack = receiver.receivedPublishes.front();
    pubPack.parsePublishData();

    QCOMPARE(pubPack.getPublishData().topic, "my/will");
    QCOMPARE(pubPack.getPublishData().payload, "mypayload");
    QCOMPARE(pubPack.getPublishData().qos, 0);
}

/**
 * @brief MainTests::testDisabledWills copied from testMqtt3will, but then wills disabled.
 */
void MainTests::testDisabledWills()
{
    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous yes");
    confFile.writeLine("wills_enabled no");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will";
    will->payload = "mypayload";
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt311);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311);
    receiver.subscribe("my/will", 0);

    sender.reset();

    usleep(500000);

    receiver.waitForMessageCount(0);

    QVERIFY(receiver.receivedPublishes.empty());
}

/**
 * @brief MainTests::testMqtt5DelayedWillsDisabled same as testMqtt5DelayedWill, but then with wills disabled.
 */
void MainTests::testMqtt5DelayedWillsDisabled()
{
    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous yes");
    confFile.writeLine("wills_enabled no");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "my/will/testMqtt5DelayedWill";
    will->payload = "mypayload";
    will->constructPropertyBuilder();
    will->propertyBuilder->writeWillDelay(1);
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt5, true, 60);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5, true, 60);
    receiver.subscribe("my/will/testMqtt5DelayedWill", 0);

    receiver.clearReceivedLists();

    sender.reset();

    usleep(4000000);
    QVERIFY(receiver.receivedPackets.empty());

    receiver.waitForMessageCount(0);

    usleep(250000);
    QVERIFY(receiver.receivedPackets.empty());
}
