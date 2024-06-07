#include "maintests.h"
#include "flashmqtestclient.h"
#include "conffiletemp.h"
#include "testhelpers.h"

#include "utils.h"
#include "retainedmessagesdb.h"

void MainTests::test_retained()
{
    std::vector<ProtocolVersion> protocols {ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5};

    for (const ProtocolVersion senderVersion : protocols)
    {
        for (const ProtocolVersion receiverVersion : protocols)
        {
            FlashMQTestClient sender;
            FlashMQTestClient receiver;

            sender.start();
            receiver.start();

            const std::string payload = "We are testing";
            const std::string topic = "retaintopic";

            sender.connectClient(senderVersion);

            Publish pub1(topic, payload, 0);
            pub1.retain = true;
            sender.publish(pub1);

            Publish pub2("dummy2", "Nobody sees this", 0);
            pub2.retain = true;
            sender.publish(pub2);

            receiver.connectClient(receiverVersion);
            receiver.subscribe("dummy", 0);
            receiver.subscribe(topic, 0);

            receiver.waitForMessageCount(1);

            MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);

            MqttPacket &msg = receiver.receivedPublishes.front();
            QCOMPARE(msg.getPayloadCopy(), payload);
            QCOMPARE(msg.getTopic(), topic);
            QVERIFY(msg.getRetain());

            receiver.clearReceivedLists();

            sender.publish(pub1);
            receiver.waitForMessageCount(1);

            QVERIFY2(receiver.receivedPublishes.size() == 1, "There must be one message in the received list");
            MqttPacket &msg2 = receiver.receivedPublishes.front();
            QCOMPARE(msg2.getPayloadCopy(), payload);
            QCOMPARE(msg2.getTopic(), topic);
            QVERIFY2(!msg2.getRetain(), "Getting a retained message while already being subscribed must be marked as normal, not retain.");
        }
    }
}

/**
 * @brief MainTests::test_retained_double_set Test incepted because of different locking paths in first tree node and second tree node.
 */
void MainTests::test_retained_double_set()
{
    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string topic = "one/two/three";

    sender.connectClient(ProtocolVersion::Mqtt5);

    {
        Publish pub("one", "dummy node creator", 0);
        pub.retain = true;
        sender.publish(pub);
    }

    Publish pub1(topic, "nobody sees this", 0);
    pub1.retain = true;
    sender.publish(pub1);

    pub1.payload = "We are setting twice";
    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("#", 0);

    receiver.waitForMessageCount(2);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 2);

    MqttPacket &msg = *std::find_if(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](const MqttPacket &p) {return p.getTopic() == "one";});
    QCOMPARE(msg.getPayloadCopy(), "dummy node creator");
    QCOMPARE(msg.getTopic(), "one");
    QVERIFY(msg.getRetain());

    MqttPacket &msg2 = *std::find_if(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [&](const MqttPacket &p) {return p.getTopic() == topic;});
    QCOMPARE(msg2.getPayloadCopy(), "We are setting twice");
    QCOMPARE(msg2.getTopic(), topic);
    QVERIFY(msg2.getRetain());
}

/**
 * @brief MainTests::test_retained_disabled Copied from test_retained and adjusted
 */
void MainTests::test_retained_mode_drop()
{
    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous yes");
    confFile.writeLine("retained_messages_mode drop");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::vector<ProtocolVersion> protocols {ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5};

    for (const ProtocolVersion senderVersion : protocols)
    {
        for (const ProtocolVersion receiverVersion : protocols)
        {
            FlashMQTestClient sender;
            FlashMQTestClient receiver;

            sender.start();
            receiver.start();

            const std::string payload = "We are testing";
            const std::string topic = "retaintopic";

            sender.connectClient(senderVersion);

            Publish pub1(topic, payload, 0);
            pub1.retain = true;
            sender.publish(pub1);

            Publish pub2("dummy2", "Nobody sees this", 0);
            pub2.retain = true;
            sender.publish(pub2);

            receiver.connectClient(receiverVersion);
            receiver.subscribe("dummy", 0);
            receiver.subscribe(topic, 0);

            usleep(250000);

            receiver.waitForMessageCount(0);
            QVERIFY2(receiver.receivedPublishes.empty(), "In drop mode, retained publishes should be stored as retained messages.");

            receiver.clearReceivedLists();

            sender.publish(pub1);

            usleep(250000);
            receiver.waitForMessageCount(0);

            QVERIFY(receiver.receivedPublishes.empty());
        }
    }
}

/**
 * @brief MainTests::test_retained_mode_downgrade copied from test_retained and adjusted
 */
void MainTests::test_retained_mode_downgrade()
{
    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous yes");
    confFile.writeLine("retained_messages_mode downgrade");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::vector<ProtocolVersion> protocols {ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5};

    for (const ProtocolVersion senderVersion : protocols)
    {
        for (const ProtocolVersion receiverVersion : protocols)
        {
            FlashMQTestClient sender;
            FlashMQTestClient receiver;

            sender.start();
            receiver.start();

            const std::string payload = "We are testing";
            const std::string topic = "retaintopic";

            sender.connectClient(senderVersion);

            Publish pub1(topic, payload, 0);
            pub1.retain = true;
            sender.publish(pub1);

            Publish pub2("dummy2", "Nobody sees this", 0);
            pub2.retain = true;
            sender.publish(pub2);

            receiver.connectClient(receiverVersion);
            receiver.subscribe("dummy", 0);
            receiver.subscribe(topic, 0);

            usleep(250000);

            receiver.waitForMessageCount(0);
            QVERIFY2(receiver.receivedPublishes.empty(), "In downgrade mode, retained publishes should not be stored as retained messages.");

            receiver.clearReceivedLists();

            sender.publish(pub1);
            receiver.waitForMessageCount(1);

            MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
            MqttPacket &msg2 = receiver.receivedPublishes.front();
            QCOMPARE(msg2.getPayloadCopy(), payload);
            QCOMPARE(msg2.getTopic(), topic);
            QVERIFY2(!msg2.getRetain(), "Getting a retained message while already being subscribed must be marked as normal, not retain.");
        }
    }
}

void MainTests::test_retained_changed()
{
    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt311);

    const std::string topic = "retaintopic";

    Publish p(topic, "We are testing", 0);
    p.retain = true;
    sender.publish(p);

    p.payload = "Changed payload";
    sender.publish(p);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe(topic, 0);

    receiver.waitForMessageCount(1);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);

    MqttPacket &pack = receiver.receivedPublishes.front();
    QCOMPARE(pack.getPayloadCopy(), p.payload);
    QVERIFY(pack.getRetain());
}

void MainTests::test_retained_removed()
{
    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    std::string payload = "We are testing";
    std::string topic = "retaintopic";

    sender.connectClient(ProtocolVersion::Mqtt311);

    Publish pub1(topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    pub1.payload = "";

    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt311);
    receiver.subscribe(topic, 0);
    usleep(100000);
    receiver.waitForMessageCount(0);

    QVERIFY2(receiver.receivedPublishes.empty(), "We erased the retained message. We shouldn't have received any.");
}

/**
 * @brief MainTests::test_retained_tree tests a bug I found, where '+/+' yields different results than '#', where it should be the same.
 */
void MainTests::test_retained_tree()
{
    FlashMQTestClient sender;
    sender.start();

    std::string payload = "We are testing";
    const std::string topic1 = "TopicA/B";
    const std::string topic2 = "Topic/C";
    const std::string topic3 = "TopicB/C";
    const std::list<std::string> topics {topic1, topic2, topic3};

    sender.connectClient(ProtocolVersion::Mqtt311);

    Publish p1(topic1, payload, 0);
    p1.retain = true;
    sender.publish(p1);

    Publish p2(topic2, payload, 0);
    p2.retain = true;
    sender.publish(p2);

    Publish p3(topic3, payload, 0);
    p3.retain = true;
    sender.publish(p3);

    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5);

    receiver.subscribe("+/+", 0);

    receiver.waitForMessageCount(3);

    QCOMPARE(receiver.receivedPublishes.size(), topics.size());

    for (const std::string &s : topics)
    {
        bool r = std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [&](MqttPacket &pack)
        {
            return pack.getTopic() == s && pack.getPayloadCopy() == payload;
        });

        QVERIFY2(r, formatString("%s not found in retained messages.", s.c_str()).c_str());
    }

}

void MainTests::test_retained_global_expire()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous yes");
    confFile.writeLine("expire_retained_messages_after_seconds 1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::vector<ProtocolVersion> protocols {ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5};

    for (const ProtocolVersion senderVersion : protocols)
    {
        for (const ProtocolVersion receiverVersion : protocols)
        {
            FlashMQTestClient sender;
            FlashMQTestClient receiver;

            sender.start();
            receiver.start();

            sender.connectClient(senderVersion);

            Publish pub1("retaintopic/1", "We are testing", 0);
            pub1.retain = true;
            sender.publish(pub1);

            Publish pub2("retaintopic/2", "asdf", 0);
            pub2.retain = true;
            sender.publish(pub2);

            usleep(2000000);

            receiver.connectClient(receiverVersion);
            receiver.subscribe("#", 0);

            usleep(500000);
            receiver.waitForMessageCount(0);

            MYCASTCOMPARE(receiver.receivedPublishes.size(), 0);
        }
    }
}

void MainTests::test_retained_per_message_expire()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous yes");
    confFile.writeLine("expire_retained_messages_after_seconds 10");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1("retaintopic/1", "We are testing", 0);
    pub1.retain = true;
    sender.publish(pub1);

    Publish pub2("retaintopic/2", "asdf", 0);
    pub2.retain = true;
    pub2.setExpireAfter(1);
    sender.publish(pub2);

    usleep(2000000);

    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("#", 0);

    usleep(500000);
    receiver.waitForMessageCount(1);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);

    MqttPacket &msg = receiver.receivedPublishes.front();
    QCOMPARE(msg.getPayloadCopy(), "We are testing");
    QCOMPARE(msg.getTopic(), "retaintopic/1");
    QVERIFY(msg.getRetain());
}

void MainTests::test_retained_tree_purging()
{
    std::shared_ptr<SubscriptionStore> store = mainApp->getStore();

    int toDeleteCount = 0;

    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            std::string topic = formatString("retain%d/bla%d/asdf", i, j);
            Publish pub(topic, "willnotexpire", 0);

            if (i % 2 == 0)
            {
                pub.setExpireAfter(1);
                pub.payload = "willexpire";

                toDeleteCount++;
            }

            std::vector<std::string> subtopics = splitTopic(topic);
            store->setRetainedMessage(pub, subtopics);
        }
    }

    {
        Publish pubStray("retain0/bla5", "willnotexpire", 0);
        std::vector<std::string> subtopics = splitTopic(pubStray.topic);
        store->setRetainedMessage(pubStray, subtopics);
    }

    usleep(2000000);

    const int beforeCount = store->getRetainedMessageCount();

    store->expireRetainedMessages();

    std::vector<RetainedMessage> list;
    const std::chrono::time_point<std::chrono::steady_clock> limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    std::deque<std::weak_ptr<RetainedMessageNode>> deferred;
    store->getRetainedMessages(store->retainedMessagesRoot.get(), list, limit, deferred);

    QVERIFY(deferred.empty());

    QVERIFY(std::none_of(list.begin(), list.end(), [](RetainedMessage &rm) {
        return rm.publish.payload == "willexpire";
    }));

    QVERIFY(std::all_of(list.begin(), list.end(), [](RetainedMessage &rm) {
        return rm.publish.payload == "willnotexpire";
    }));

    MYCASTCOMPARE(store->getRetainedMessageCount(), beforeCount - toDeleteCount);
}

void MainTests::testRetainAsPublished()
{
    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient client2;
    client2.start();
    client2.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient client3;
    client3.start();
    client3.connectClient(ProtocolVersion::Mqtt5);

    client2.subscribe("mytopic", 1, false, false);
    client.subscribe("mytopic", 1, false, true);
    client3.subscribe("mytopic", 1, false, false);

    try
    {
        Publish pub("mytopic", "mypayload", 1);
        pub.retain = true;
        client.publish(pub);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }

    try
    {
        client.waitForMessageCount(1);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }

    MYCASTCOMPARE(client.receivedPublishes.size(), 1);

    const MqttPacket &first = client.receivedPublishes.front();

    QVERIFY(first.getRetain());
}

void MainTests::testRetainAsPublishedNegative()
{
    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5);

    client.subscribe("mytopic", 1, false, false);

    try
    {
        Publish pub("mytopic", "mypayload", 1);
        pub.retain = true;
        client.publish(pub);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }

    try
    {
        client.waitForMessageCount(1);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }

    MYCASTCOMPARE(client.receivedPublishes.size(), 1);

    const MqttPacket &first = client.receivedPublishes.front();

    QVERIFY(!first.getRetain());
}

/**
 * @brief MainTests::testRetainedParentOfWildcard tests whether subscribing to 'one/two/three/four/#' gives you 'one/two/three/four'.
 */
void MainTests::testRetainedParentOfWildcard()
{
    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "We are testing testRetainedParentOfWildcard";
    const std::string publish_topic = "one/two/three/four";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(publish_topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("dummy", 0);
    receiver.subscribe("one/two/three/four/#", 0);

    try
    {
        receiver.waitForMessageCount(1);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, "Exception happened. Likely waited for retained messages, but none received.");
    }

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);

    MqttPacket &msg = receiver.receivedPublishes.front();
    QCOMPARE(msg.getPayloadCopy(), payload);
    QCOMPARE(msg.getTopic(), publish_topic);
    QVERIFY(msg.getRetain());
}

void MainTests::testRetainedWildcard()
{
    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    receiver.start();

    const std::string payload = "We are testing testRetainedWildcard";
    const std::string publish_topic = "one/two/three/four";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(publish_topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    Publish pub2("publish/into/nothing", payload, 0);
    pub2.retain = true;
    sender.publish(pub2);

    receiver.connectClient(ProtocolVersion::Mqtt5);
    receiver.subscribe("dummy", 0);
    receiver.subscribe("one/two/three/#", 0);

    try
    {
        receiver.waitForMessageCount(1);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, "Exception happened. Likely waited for retained messages, but none received.");
    }

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);

    MqttPacket &msg = receiver.receivedPublishes.front();
    QCOMPARE(msg.getPayloadCopy(), payload);
    QCOMPARE(msg.getTopic(), publish_topic);
    QVERIFY(msg.getRetain());
}

/**
 * @brief MainTests::testRetainedAclReadCheck tests the manipulation of the retain bit in the original incoming packet.
 *
 * This has to do with the optimization in CopyFactory, that under certain conditions, the original packet's vector is
 * just used to write to the client.
 */
void MainTests::testRetainedAclReadCheck()
{
    ConfFileTemp confFile;
    confFile.writeLine("plugin plugins/libtest_plugin.so.0.0.1");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5, true, 30, [](Connect &connect) {
        connect.clientid = "test_user_with_retain_as_published_v8sIeCvI";
    });

    client.subscribe("mytopic", 1, false, true);

    FlashMQTestClient client2;
    client2.start();
    client2.connectClient(ProtocolVersion::Mqtt5, true, 30, [](Connect &connect) {
        connect.clientid = "test_user_without_retain_as_published_CswU21YA";
    });

    client2.subscribe("mytopic", 1, false, false);

    FlashMQTestClient publish_client;
    publish_client.start();
    publish_client.connectClient(ProtocolVersion::Mqtt5);

    Publish pub("mytopic", "mypayload", 1);
    pub.retain = true;
    publish_client.publish(pub);

    try
    {
        client.waitForMessageCount(1);
        client2.waitForMessageCount(1);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }

    MYCASTCOMPARE(client.receivedPublishes.size(), 1);
    MYCASTCOMPARE(client2.receivedPublishes.size(), 1);
}


