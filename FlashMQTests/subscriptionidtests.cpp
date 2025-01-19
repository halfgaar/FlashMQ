#include "maintests.h"
#include "flashmqtestclient.h"
#include "testhelpers.h"

void MainTests::testSubscriptionIdOnlineClient()
{
    FlashMQTestClient client1;
    client1.start();
    client1.connectClient(ProtocolVersion::Mqtt5);
    client1.subscribe("several/sub/topics", 1, false, false, 666);

    FlashMQTestClient client2;
    client2.start();
    client2.connectClient(ProtocolVersion::Mqtt5);
    client2.subscribe("several/sub/topics", 1, false, false, 777);

    // Also one without an identifier.
    FlashMQTestClient client3;
    client3.start();
    client3.connectClient(ProtocolVersion::Mqtt5);
    client3.subscribe("several/sub/topics", 1, false, false);

    {
        FlashMQTestClient sender;
        sender.start();
        sender.connectClient(ProtocolVersion::Mqtt5);

        Publish pub("several/sub/topics", "payload", 1);
        sender.publish(pub);
    }

    client1.waitForMessageCount(1);
    client2.waitForMessageCount(1);
    client3.waitForMessageCount(1);

    {
        auto ro = client1.receivedObjects.lock();
        auto &pack = ro->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(666));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }

    {
        auto ro = client2.receivedObjects.lock();
        auto &pack = ro->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(777));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }

    {
        auto ro = client3.receivedObjects.lock();
        auto &pack = ro->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(0));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }
}

void MainTests::testSubscriptionIdOfflineClient()
{
    std::optional<FlashMQTestClient> client1;
    client1.emplace();
    client1->start();
    client1->connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &c) {
        c.clientid = "one";
    });
    client1->subscribe("several/sub/topics", 1, false, false, 42);
    client1.reset();

    std::optional<FlashMQTestClient> client2;
    client2.emplace();
    client2->start();
    client2->connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &c) {
        c.clientid = "two";
    });
    client2->subscribe("several/sub/topics", 1, false, false, 99);
    client2.reset();

    {
        FlashMQTestClient sender;
        sender.start();
        sender.connectClient(ProtocolVersion::Mqtt5);

        Publish pub("several/sub/topics", "payload", 1);
        sender.publish(pub);
    }

    client1.emplace();
    client1->start();
    client1->connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &c) {
        c.clientid = "one";
    });

    client2.emplace();
    client2->start();
    client2->connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &c) {
        c.clientid = "two";
    });

    client1->waitForMessageCount(1);
    client2->waitForMessageCount(1);

    {
        auto ro = client1.value().receivedObjects.lock();
        auto &pack = ro->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(42));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }

    {
        auto ro = client2.value().receivedObjects.lock();
        auto &pack = ro->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(99));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }
}

void MainTests::testSubscriptionIdRetainedMessages()
{
    FlashMQTestClient sender;
    sender.start();

    const std::string payload = "We are testing";
    const std::string topic = "retaintopic";

    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub1(topic, payload, 0);
    pub1.retain = true;
    sender.publish(pub1);

    FlashMQTestClient receiver1;
    receiver1.start();
    receiver1.connectClient(ProtocolVersion::Mqtt5);
    receiver1.subscribe("dummy", 0);
    receiver1.subscribe(topic, 0, false, false, 123);
    receiver1.waitForMessageCount(1);

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt5);
    receiver2.subscribe("dummy", 0);
    receiver2.subscribe(topic, 0, false, false, 1000000);
    receiver2.waitForMessageCount(1);

    {
        auto ro1 = receiver1.receivedObjects.lock();
        auto ro2 = receiver2.receivedObjects.lock();

        MYCASTCOMPARE(ro1->receivedPublishes.size(), 1);
        MYCASTCOMPARE(ro2->receivedPublishes.size(), 1);

        MqttPacket &msg = ro1->receivedPublishes.front();
        QCOMPARE(msg.getPayloadCopy(), payload);
        QCOMPARE(msg.getTopic(), topic);
        QVERIFY(msg.getRetain());
    }

    {
        auto ro1 = receiver1.receivedObjects.lock();
        auto &pack = ro1->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(123));
        FMQ_COMPARE(pack.getTopic(), topic);
        FMQ_COMPARE(pack.getPayloadView(), payload);
        FMQ_VERIFY(pack.getRetain());
    }

    {
        auto ro2 = receiver2.receivedObjects.lock();
        auto &pack = ro2->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(1000000));
        FMQ_COMPARE(pack.getTopic(), topic);
        FMQ_COMPARE(pack.getPayloadView(), payload);
        FMQ_VERIFY(pack.getRetain());
    }
}

void MainTests::testSubscriptionIdSharedSubscriptions()
{
    FlashMQTestClient client1;
    client1.start();
    client1.connectClient(ProtocolVersion::Mqtt5);
    client1.subscribe("$share/myshare/several/sub/topics", 1, false, false, 666);

    FlashMQTestClient client2;
    client2.start();
    client2.connectClient(ProtocolVersion::Mqtt5);
    client2.subscribe("$share/myshare/several/sub/topics", 1, false, false, 777);

    // Also one without an identifier.
    FlashMQTestClient client3;
    client3.start();
    client3.connectClient(ProtocolVersion::Mqtt5);
    client3.subscribe("$share/myshare/several/sub/topics", 1, false, false);

    {
        FlashMQTestClient sender;
        sender.start();
        sender.connectClient(ProtocolVersion::Mqtt5);

        Publish pub("several/sub/topics", "payload", 1);
        for (int i = 0; i < 3; i++)
            sender.publish(pub);
    }

    client1.waitForMessageCount(1);
    client2.waitForMessageCount(1);
    client3.waitForMessageCount(1);

    {
        auto ro1 = client1.receivedObjects.lock();
        FMQ_COMPARE(ro1->receivedPublishes.size(), static_cast<size_t>(1));
        auto &pack = ro1->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(666));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }

    {
        auto ro2 = client2.receivedObjects.lock();
        FMQ_COMPARE(ro2->receivedPublishes.size(), static_cast<size_t>(1));
        auto &pack = ro2->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(777));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }

    {
        auto ro3 = client3.receivedObjects.lock();
        FMQ_COMPARE(ro3->receivedPublishes.size(), static_cast<size_t>(1));
        auto &pack = ro3->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(0));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }
}

void MainTests::testSubscriptionIdChange()
{
    FlashMQTestClient client1;
    client1.start();
    client1.connectClient(ProtocolVersion::Mqtt5);
    client1.subscribe("several/sub/topics", 1, false, false, 666);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    Publish pub("several/sub/topics", "payload", 1);
    sender.publish(pub);

    client1.waitForMessageCount(1);

    {
        auto ro = client1.receivedObjects.lock();
        auto &pack = ro->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(666));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }

    // Now we subscribe again, but with a different identifier.
    client1.subscribe("several/sub/topics", 1, false, false, 667);

    sender.publish(pub);

    client1.waitForMessageCount(1);

    {
        auto ro = client1.receivedObjects.lock();
        auto &pack = ro->receivedPublishes.at(0);
        FMQ_COMPARE(pack.publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(667));
        FMQ_COMPARE(pack.getTopic(), "several/sub/topics");
        FMQ_COMPARE(pack.getPayloadView(), "payload");
    }

}

void MainTests::testSubscriptionIdOverlappingSubscriptions()
{
    FlashMQTestClient client1;
    client1.start();
    client1.connectClient(ProtocolVersion::Mqtt5);
    client1.subscribe("several/sub/topics", 1, false, false, 666);
    client1.subscribe("several/#", 1, false, false, 999);

    {
        FlashMQTestClient sender;
        sender.start();
        sender.connectClient(ProtocolVersion::Mqtt5);

        Publish pub("several/sub/topics", "payload", 1);
        sender.publish(pub);
    }

    client1.waitForMessageCount(2);

    {
        auto ro = client1.receivedObjects.lock();

        auto pos = std::find_if(ro->receivedPublishes.begin(), ro->receivedPublishes.end(), [] (MqttPacket &p) {
            return p.publishData.subscriptionIdentifierTesting == 999;
        });

        FMQ_VERIFY(pos != ro->receivedPublishes.end());

        FMQ_COMPARE(pos->publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(999));
        FMQ_COMPARE(pos->getTopic(), "several/sub/topics");
        FMQ_COMPARE(pos->getPayloadView(), "payload");
    }

    {
        auto ro = client1.receivedObjects.lock();

        auto pos = std::find_if(ro->receivedPublishes.begin(), ro->receivedPublishes.end(), [] (MqttPacket &p) {
            return p.publishData.subscriptionIdentifierTesting == 666;
        });

        FMQ_VERIFY(pos != ro->receivedPublishes.end());

        FMQ_COMPARE(pos->publishData.subscriptionIdentifierTesting, static_cast<uint32_t>(666));
        FMQ_COMPARE(pos->getTopic(), "several/sub/topics");
        FMQ_COMPARE(pos->getPayloadView(), "payload");
    }
}


