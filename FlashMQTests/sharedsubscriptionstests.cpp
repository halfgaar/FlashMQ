#include "maintests.h"
#include "testhelpers.h"
#include "flashmqtestclient.h"
#include "flashmqtempdir.h"
#include "conffiletemp.h"

#include "utils.h"
#include "threadglobals.h"

void MainTests::testSharedSubscribersUnit()
{
    Settings settings;
    PluginLoader pluginLoader;
    std::shared_ptr<ThreadData> t(new ThreadData(0, settings, pluginLoader));

    // Kind of a hack...
    Authentication auth(settings);
    ThreadGlobals::assign(&auth);
    ThreadGlobals::assignThreadData(t.get());

    std::shared_ptr<Client> c1(new Client(0, t, nullptr, false, false, nullptr, settings, false));
    c1->setClientProperties(ProtocolVersion::Mqtt5, "clientid1", "user1", true, 60);

    std::shared_ptr<Client> c2(new Client(0, t, nullptr, false, false, nullptr, settings, false));
    c2->setClientProperties(ProtocolVersion::Mqtt5, "clientid2", "user2", true, 60);

    std::shared_ptr<Client> c3(new Client(0, t, nullptr, false, false, nullptr, settings, false));
    c3->setClientProperties(ProtocolVersion::Mqtt5, "clientid3", "user3", true, 60);

    std::shared_ptr<Session> ses1 = std::make_shared<Session>(c1->getClientId(), c1->getUsername());
    ses1->assignActiveConnection(c1);

    std::shared_ptr<Session> ses2 = std::make_shared<Session>(c2->getClientId(), c2->getUsername());
    ses2->assignActiveConnection(c2);

    std::shared_ptr<Session> ses3 = std::make_shared<Session>(c3->getClientId(), c3->getUsername());
    ses3->assignActiveConnection(c3);

    SharedSubscribers s;

    s[ses1->getClientId()].session = ses1;
    MYCASTCOMPARE(s.members.size(), 1);

    s[ses2->getClientId()].session = ses2;
    MYCASTCOMPARE(s.members.size(), 2);

    s[ses3->getClientId()].session = ses3;
    MYCASTCOMPARE(s.members.size(), 3);

    s[ses2->getClientId()].reset();
    MYCASTCOMPARE(s.members.size(), 3);

    QCOMPARE(*s.getNext(), s[ses1->getClientId()]);
    QCOMPARE(*s.getNext(), s[ses3->getClientId()]);

    s.purgeAndReIndex();
    MYCASTCOMPARE(s.members.size(), 2);

    // We still should get the same two active members
    QCOMPARE(*s.getNext(), s[ses1->getClientId()]);
    QCOMPARE(*s.getNext(), s[ses3->getClientId()]);

    s.erase(ses3->getClientId());

    // Now we only have one left
    QCOMPARE(*s.getNext(), s[ses1->getClientId()]);
    QCOMPARE(*s.getNext(), s[ses1->getClientId()]);

    s.erase(ses1->getClientId());
    QVERIFY(!s.empty());
    s.purgeAndReIndex();
    QVERIFY(s.empty());
}

void MainTests::testSharedSubscribers()
{
    FlashMQTestClient receiver1;
    receiver1.start();
    receiver1.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt5);

    receiver1.subscribe("$share/ahTahHu5/one/two/three", 1);
    receiver2.subscribe("$share/ahTahHu5/one/two/three", 1);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    sender.publish("one/two/three", "rainy day", 1);
    sender.publish("one/two/three", "sunny day", 1);

    receiver1.waitForMessageCount(1);
    receiver2.waitForMessageCount(1);

    {
        auto ro1 = receiver1.receivedObjects.lock();
        auto ro2 = receiver2.receivedObjects.lock();

        MYCASTCOMPARE(ro1->receivedPublishes.size(), 1);
        MYCASTCOMPARE(ro2->receivedPublishes.size(), 1);

        int rain = std::count_if(ro1->receivedPublishes.begin(), ro1->receivedPublishes.end(),
                                 [](const MqttPacket &pack) { return pack.getPayloadCopy() == "rainy day";});
        int sun = std::count_if(ro2->receivedPublishes.begin(), ro2->receivedPublishes.end(),
                                [](const MqttPacket &pack) { return pack.getPayloadCopy() == "sunny day";});

        QCOMPARE(rain, 1);
        QCOMPARE(sun, 1);
    }

    receiver1.unsubscribe("$share/ahTahHu5/one/two/three");

    receiver1.clearReceivedLists();
    receiver2.clearReceivedLists();

    sender.publish("one/two/three", "received by one", 1);
    sender.publish("one/two/three", "received by one", 1);

    receiver2.waitForMessageCount(2);

    {
        auto ro1 = receiver1.receivedObjects.lock();
        auto ro2 = receiver2.receivedObjects.lock();

        MYCASTCOMPARE(ro1->receivedPublishes.size(), 0);
        MYCASTCOMPARE(ro2->receivedPublishes.size(), 2);

        QCOMPARE(ro2->receivedPublishes.at(0).getPayloadCopy(), "received by one");
    }
}

void MainTests::testDisconnectedSharedSubscribers()
{
    FlashMQTestClient receiver1;
    receiver1.start();
    receiver1.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver3;
    receiver3.start();
    receiver3.connectClient(ProtocolVersion::Mqtt5);

    receiver1.subscribe("$share/iidahs2U/one/two/three", 1);
    receiver2.subscribe("$share/iidahs2U/one/two/three", 1);
    receiver3.subscribe("$share/iidahs2U/one/two/three", 1);

    receiver2.disconnect(ReasonCodes::Success);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    sender.publish("one/two/three", "rainy day", 1);
    sender.publish("one/two/three", "sunny day", 1);

    receiver1.waitForMessageCount(1);
    receiver3.waitForMessageCount(1);

    auto ro1 = receiver1.receivedObjects.lock();
    auto ro3 = receiver3.receivedObjects.lock();

    MYCASTCOMPARE(ro1->receivedPublishes.size(), 1);
    MYCASTCOMPARE(ro3->receivedPublishes.size(), 1);

    int rain = std::count_if(ro1->receivedPublishes.begin(), ro1->receivedPublishes.end(),
                             [](const MqttPacket &pack) { return pack.getPayloadCopy() == "rainy day";});
    int sun = std::count_if(ro3->receivedPublishes.begin(), ro3->receivedPublishes.end(),
                            [](const MqttPacket &pack) { return pack.getPayloadCopy() == "sunny day";});

    QCOMPARE(rain, 1);
    QCOMPARE(sun, 1);
}

void MainTests::testUnsubscribedSharedSubscribers()
{
    FlashMQTestClient receiver1;
    receiver1.start();
    receiver1.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver3;
    receiver3.start();
    receiver3.connectClient(ProtocolVersion::Mqtt5);

    receiver1.subscribe("$share/iidahs2U/one/two/three", 1);
    receiver2.subscribe("$share/iidahs2U/one/two/three", 1);
    receiver3.subscribe("$share/iidahs2U/one/two/three", 1);

    receiver2.unsubscribe("$share/iidahs2U/one/two/three");

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    sender.publish("one/two/three", "rainy day", 1);
    sender.publish("one/two/three", "sunny day", 1);

    receiver1.waitForMessageCount(1);
    receiver3.waitForMessageCount(1);

    auto ro1 = receiver1.receivedObjects.lock();
    auto ro3 = receiver3.receivedObjects.lock();

    MYCASTCOMPARE(ro1->receivedPublishes.size(), 1);
    MYCASTCOMPARE(ro3->receivedPublishes.size(), 1);

    int rain = std::count_if(ro1->receivedPublishes.begin(), ro1->receivedPublishes.end(),
                             [](const MqttPacket &pack) { return pack.getPayloadCopy() == "rainy day";});
    int sun = std::count_if(ro3->receivedPublishes.begin(), ro3->receivedPublishes.end(),
                            [](const MqttPacket &pack) { return pack.getPayloadCopy() == "sunny day";});

    QCOMPARE(rain, 1);
    QCOMPARE(sun, 1);
}

void MainTests::testSharedSubscribersSurviveRestart()
{
    FlashMQTempDir storageDir;

    ConfFileTemp confFile;
    confFile.writeLine(formatString("storage_dir %s", storageDir.getPath().c_str()));
    confFile.writeLine("allow_anonymous yes");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient receiver1;
    receiver1.start();
    receiver1.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect){
        connect.clientid = "Receiver1";
    });

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect){
        connect.clientid = "Receiver2";
    });

    receiver1.subscribe("$share/kw3O9fGK/one/two/three", 1);
    receiver2.subscribe("$share/kw3O9fGK/one/two/three", 1);

    // Restart the server.
    cleanup();
    init(args);

    receiver1.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect){
        connect.clientid = "Receiver1";
    });

    receiver2.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect){
        connect.clientid = "Receiver2";
    });

    // Now that we should have resumed sessions, perform a test like testSharedSubscribers()

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    sender.publish("one/two/three", "rainy day", 1);
    sender.publish("one/two/three", "sunny day", 1);

    receiver1.waitForMessageCount(1);
    receiver2.waitForMessageCount(1);

    auto ro1 = receiver1.receivedObjects.lock();
    auto ro2 = receiver2.receivedObjects.lock();

    MYCASTCOMPARE(ro1->receivedPublishes.size(), 1);
    MYCASTCOMPARE(ro2->receivedPublishes.size(), 1);

    int rain = std::count_if(ro1->receivedPublishes.begin(), ro1->receivedPublishes.end(),
                             [](const MqttPacket &pack) { return pack.getPayloadCopy() == "rainy day";});
    int sun = std::count_if(ro2->receivedPublishes.begin(), ro2->receivedPublishes.end(),
                            [](const MqttPacket &pack) { return pack.getPayloadCopy() == "sunny day";});

    QCOMPARE(rain, 1);
    QCOMPARE(sun, 1);
}

void MainTests::testSharedSubscriberDoesntGetRetainedMessages()
{
    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "We are testing";
    const std::string topic = "$share/sharename/retaintopic";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe(topic, 0);

    usleep(250000);

    auto ro = receiver.receivedObjects.lock();

    MYCASTCOMPARE(ro->receivedPublishes.size(), 0);
}
