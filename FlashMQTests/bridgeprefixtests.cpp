#include "maintests.h"
#include "conffiletemp.h"
#include "flashmqtestclient.h"
#include "mainappasfork.h"
#include "testhelpers.h"
#include "flashmqtempdir.h"

void waitForMessagesOverBridge(FlashMQTestClient &one, FlashMQTestClient &two, const std::string &topic)
{
    int wait_i = 0;
    for(wait_i = 0; wait_i < 10; wait_i++)
    {
        one.publish(topic, "connectiontest", 2);

        try
        {
            two.waitForMessageCount(1);
            break;
        }
        catch (std::exception &ex) { }
    }

    if (wait_i >= 10)
        throw std::runtime_error("Timeout waiting for messages over bridge");
}

void MainTests::forkingTestBridgeWithLocalAndRemotePrefix()
{
    for (const std::string protocol_version : {"mqtt5", "mqtt3.1"})
    {
        cleanup();

        ConfFileTemp confFile;
        const std::string config = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 21883
    subscribe ManglerRemote/shoes 2
    publish ManglerLocal/boots 2
    clientid_prefix Mangler
    local_prefix ManglerLocal/
    remote_prefix ManglerRemote/
    protocol_version %s
}
listen {
    protocol mqtt
    port 51183
})";
        confFile.writeLine(formatString(config, protocol_version.c_str()));
        confFile.closeFile();

        std::vector<std::string> args {"--config-file", confFile.getFilePath()};

        MainAppAsFork app(args);
        app.start();
        app.waitForStarted(51183);

        // We will consider the test server initialized here the 'remote' broker.
        init();

        FlashMQTestClient clientToLocalWithBridge;
        clientToLocalWithBridge.start();

        FlashMQTestClient clientToRemote;
        clientToRemote.start();

        clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt31, 51183);
        clientToLocalWithBridge.subscribe("#", 1);

        clientToRemote.connectClient(ProtocolVersion::Mqtt5);
        clientToRemote.subscribe("#", 1);

        waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "ManglerLocal/boots");

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToLocalWithBridge.publish("ManglerLocal/boots", "asdf", 2);
            clientToRemote.waitForMessageCount(1);
            auto ro = clientToRemote.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerRemote/boots"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "asdf");
        }

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToRemote.publish("ManglerRemote/shoes", "are made for walking", 2);
            clientToLocalWithBridge.waitForMessageCount(1);
            auto ro = clientToLocalWithBridge.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerLocal/shoes"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "are made for walking");
        }
    }
}

/**
 * @brief Test the internal packet cache; that we don't accidentally cache packets with the prefixes applied.
 *
 * The PublishCopyFactory was temporarily broken first to write this test.
 */
void MainTests::forkingTestBridgePrefixesOtherClientsUnaffected()
{
    for (const std::string protocol_version : {"mqtt5", "mqtt3.1"})
    {
        cleanup();

        ConfFileTemp confFile;
        const std::string config = R"(
allow_anonymous true
log_debug false
log_file /tmp/ppp.log

bridge {
    address ::1
    port 21883
    subscribe ManglerRemote/shoes 2
    publish ManglerLocal/boots 2
    clientid_prefix Mangler
    local_prefix ManglerLocal/
    remote_prefix ManglerRemote/
    protocol_version %s
}
listen {
    protocol mqtt
    port 51183
})";
        confFile.writeLine(formatString(config, protocol_version.c_str()));
        confFile.closeFile();

        std::vector<std::string> args {"--config-file", confFile.getFilePath()};

        MainAppAsFork app(args);
        app.start();
        app.waitForStarted(51183);

        // We will consider the test server initialized here the 'remote' broker.
        init();

        FlashMQTestClient randomOtherLocalClient1;
        randomOtherLocalClient1.start();
        randomOtherLocalClient1.connectClient(ProtocolVersion::Mqtt5, 51183);

        // Not using wild-cards because we happen to know that produces the correct delivery order to subscribers to test this.
        randomOtherLocalClient1.subscribe("ManglerLocal/boots", 1);
        randomOtherLocalClient1.subscribe("ManglerRemote/boots", 1);

        FlashMQTestClient clientToLocalWithBridge;
        clientToLocalWithBridge.start();

        FlashMQTestClient clientToRemote;
        clientToRemote.start();

        clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt31, 51183);
        clientToLocalWithBridge.subscribe("#", 1);

        clientToRemote.connectClient(ProtocolVersion::Mqtt5);
        clientToRemote.subscribe("#", 1);

        FlashMQTestClient randomOtherLocalClient2;
        randomOtherLocalClient2.start();
        randomOtherLocalClient2.connectClient(ProtocolVersion::Mqtt5, 51183);

        // Not using wild-cards because we happen to know that produces the correct delivery order to subscribers to test this.
        randomOtherLocalClient2.subscribe("ManglerLocal/boots", 1);
        randomOtherLocalClient2.subscribe("ManglerRemote/boots", 1);

        FlashMQTestClient randomOtherLocalClient3;
        randomOtherLocalClient3.start();
        randomOtherLocalClient3.connectClient(ProtocolVersion::Mqtt5, 51183);

        // For this one, we do use the wildcard subscription, just to be sure.
        randomOtherLocalClient3.subscribe("ManglerLocal/#", 1);

        waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "ManglerLocal/boots");

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToLocalWithBridge.publish("ManglerLocal/boots", "asdf", 2);
            clientToRemote.waitForMessageCount(1);
            auto ro = clientToRemote.receivedObjects.lock();
            //FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerRemote/boots"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "asdf");
        }

        clientToLocalWithBridge.clearReceivedLists();


        {
            randomOtherLocalClient1.waitForMessageCount(2);
            auto ro = randomOtherLocalClient1.receivedObjects.lock();
            FMQ_VERIFY(std::all_of(ro->receivedPublishes.begin(), ro->receivedPublishes.end(), [](MqttPacket &p) {
                return startsWith(p.getTopic(), "ManglerLocal/");
            }));
        }

        {
            randomOtherLocalClient2.waitForMessageCount(2);
            auto ro = randomOtherLocalClient2.receivedObjects.lock();
            FMQ_VERIFY(std::all_of(ro->receivedPublishes.begin(), ro->receivedPublishes.end(), [](MqttPacket &p) {
                return startsWith(p.getTopic(), "ManglerLocal/");
            }));
        }

        {
            randomOtherLocalClient3.waitForMessageCount(2);
            auto ro = randomOtherLocalClient3.receivedObjects.lock();
            FMQ_VERIFY(std::all_of(ro->receivedPublishes.begin(), ro->receivedPublishes.end(), [](MqttPacket &p) {
                return startsWith(p.getTopic(), "ManglerLocal/");
            }));
        }
    }
}

void MainTests::forkingTestBridgeWithOnlyRemotePrefix()
{
    for (const std::string protocol_version : {"mqtt5", "mqtt3.1"})
    {
        cleanup();

        ConfFileTemp confFile;
        const std::string config = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 21883
    subscribe ManglerRemote/shoes 2
    publish boots 2
    clientid_prefix Mangler
    remote_prefix ManglerRemote/
    protocol_version %s
}
listen {
    protocol mqtt
    port 51183
})";
        confFile.writeLine(formatString(config, protocol_version.c_str()));
        confFile.closeFile();

        std::vector<std::string> args {"--config-file", confFile.getFilePath()};

        MainAppAsFork app(args);
        app.start();
        app.waitForStarted(51183);

        // We will consider the test server initialized here the 'remote' broker.
        init();

        FlashMQTestClient clientToLocalWithBridge;
        clientToLocalWithBridge.start();

        FlashMQTestClient clientToRemote;
        clientToRemote.start();

        FlashMQTestClient receiverToLocal;
        receiverToLocal.start();

        FlashMQTestClient receiverToLocal2;
        receiverToLocal2.start();

        receiverToLocal2.connectClient(ProtocolVersion::Mqtt31, 51183);
        receiverToLocal2.subscribe("#", 1);

        clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt31, 51183);
        clientToLocalWithBridge.subscribe("#", 1);

        clientToRemote.connectClient(ProtocolVersion::Mqtt5);
        clientToRemote.subscribe("#", 1);

        waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "boots");

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        receiverToLocal.connectClient(ProtocolVersion::Mqtt31, 51183);
        receiverToLocal.subscribe("#", 1);

        {
            clientToLocalWithBridge.publish("boots", "asdf", 2);
            clientToRemote.waitForMessageCount(1);
            auto ro = clientToRemote.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerRemote/boots"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "asdf");
        }

        // Make sure other clients connected to the server with prefixes defined get normal topics. I.e. test packet cache.
        {
            receiverToLocal.waitForMessageCount(1);
            auto roLocal = receiverToLocal.receivedObjects.lock();
            FMQ_COMPARE(roLocal->receivedPublishes.at(0).getTopic(), std::string("boots"));
            FMQ_COMPARE(roLocal->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(roLocal->receivedPublishes.at(0).getPayloadView(), "asdf");
        }

        {
            receiverToLocal2.waitForMessageCount(1);
            auto roLocal2 = receiverToLocal2.receivedObjects.lock();
            FMQ_COMPARE(roLocal2->receivedPublishes.back().getTopic(), std::string("boots"));
            FMQ_COMPARE(roLocal2->receivedPublishes.back().getQos(), 1);
            FMQ_COMPARE(roLocal2->receivedPublishes.back().getPayloadView(), "asdf");
        }

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToRemote.publish("ManglerRemote/shoes", "are made for walking", 2);
            clientToLocalWithBridge.waitForMessageCount(1);
            auto ro = clientToLocalWithBridge.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("shoes"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "are made for walking");
        }


    }
}

void MainTests::forkingTestBridgeWithOnlyLocalPrefix()
{
    for (const std::string protocol_version : {"mqtt5", "mqtt3.1"})
    {
        cleanup();

        ConfFileTemp confFile;
        const std::string config = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 21883
    subscribe shoes 2
    publish ManglerLocal/boots 2
    clientid_prefix Mangler
    local_prefix ManglerLocal/
    protocol_version %s
}
listen {
    protocol mqtt
    port 51183
})";
        confFile.writeLine(formatString(config, protocol_version.c_str()));
        confFile.closeFile();

        std::vector<std::string> args {"--config-file", confFile.getFilePath()};

        MainAppAsFork app(args);
        app.start();
        app.waitForStarted(51183);

        // We will consider the test server initialized here the 'remote' broker.
        init();

        FlashMQTestClient clientToLocalWithBridge;
        clientToLocalWithBridge.start();

        FlashMQTestClient clientToRemote;
        clientToRemote.start();

        FlashMQTestClient senderToLocal;
        senderToLocal.start();

        clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt31, 51183);
        clientToLocalWithBridge.subscribe("#", 1);

        clientToRemote.connectClient(ProtocolVersion::Mqtt5);
        clientToRemote.subscribe("#", 1);

        waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "ManglerLocal/boots");

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToLocalWithBridge.publish("ManglerLocal/boots", "asdf", 2);
            clientToRemote.waitForMessageCount(1);
            auto ro = clientToRemote.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("boots"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "asdf");
        }

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToRemote.publish("shoes", "are made for walking", 2);
            clientToLocalWithBridge.waitForMessageCount(1);
            auto ro = clientToLocalWithBridge.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerLocal/shoes"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "are made for walking");
        }

        // Make sure other clients connected to the server with prefixes defined get normal topics.
        {
            clientToLocalWithBridge.clearReceivedLists();
            senderToLocal.connectClient(ProtocolVersion::Mqtt31, 51183);
            senderToLocal.publish("panic", "attack", 0);
            clientToLocalWithBridge.waitForMessageCount(1);
            auto ro = clientToLocalWithBridge.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("panic"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 0);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "attack");
        }
    }
}

/**
 * @brief Test that we don't apply the prefix on outgoing messages when it's the same as the topic string.
 *
 * When having a prefix of 'one/two/', this is to prevent a subscription like 'one/two/#', turning topic 'one/two/'
 * into '', which is illegal (while empty subtopic strings, like caused by trailing slash, is legal).
 */
void MainTests::forkingTestBridgeOutgoingTopicEqualsPrefix()
{
    cleanup();

    ConfFileTemp confFile;
    const std::string config = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 21883
    subscribe shoes 2
    publish ManglerLocal/# 2
    clientid_prefix Mangler
    local_prefix ManglerLocal/
    protocol_version mqtt5
}
listen {
    protocol mqtt
    port 51183
})";
    confFile.writeLine(config);
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    MainAppAsFork app(args);
    app.start();
    app.waitForStarted(51183);

    // We will consider the test server initialized here the 'remote' broker.
    init();

    FlashMQTestClient clientToLocalWithBridge;
    clientToLocalWithBridge.start();

    FlashMQTestClient clientToRemote;
    clientToRemote.start();

    FlashMQTestClient senderToLocal;
    senderToLocal.start();

    clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt31, 51183);
    clientToLocalWithBridge.subscribe("#", 1);

    clientToRemote.connectClient(ProtocolVersion::Mqtt5);
    clientToRemote.subscribe("#", 1);

    waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "ManglerLocal/boots");

    clientToLocalWithBridge.clearReceivedLists();
    clientToRemote.clearReceivedLists();

    {
        clientToLocalWithBridge.publish("ManglerLocal/", "WC4WQbYJb76TguUT", 2);
        clientToRemote.waitForMessageCount(1);
        auto ro = clientToRemote.receivedObjects.lock();
        FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerLocal/"));
        FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
        FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "WC4WQbYJb76TguUT");
    }
}

/**
 * @brief Same as forkingTestBridgeOutgoingTopicEqualsPrefix, but then for incoming.
 */
void MainTests::forkingTestBridgeIncomingTopicEqualsPrefix()
{
    cleanup();

    ConfFileTemp confFile;
    const std::string config = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 21883
    subscribe ManglerRemote/# 2
    publish boots 2
    clientid_prefix Mangler
    remote_prefix ManglerRemote/
    protocol_version mqtt5
}
listen {
    protocol mqtt
    port 51183
})";
    confFile.writeLine(config);
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    MainAppAsFork app(args);
    app.start();
    app.waitForStarted(51183);

    // We will consider the test server initialized here the 'remote' broker.
    init();

    FlashMQTestClient clientToLocalWithBridge;
    clientToLocalWithBridge.start();

    FlashMQTestClient clientToRemote;
    clientToRemote.start();

    clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt31, 51183);
    clientToLocalWithBridge.subscribe("#", 1);

    clientToRemote.connectClient(ProtocolVersion::Mqtt5);
    clientToRemote.subscribe("#", 1);

    waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "boots");

    clientToLocalWithBridge.clearReceivedLists();
    clientToRemote.clearReceivedLists();

    {
        clientToRemote.publish("ManglerRemote/", "XN0KoFeDOimgRiTs", 2);
        clientToLocalWithBridge.waitForMessageCount(1);
        auto ro = clientToLocalWithBridge.receivedObjects.lock();
        FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerRemote/"));
        FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
        FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "XN0KoFeDOimgRiTs");
    }
}

void MainTests::forkingTestBridgeZWithLocalAndRemotePrefixRetained()
{
    for (const std::string protocol_version : {"mqtt5", "mqtt3.1"})
    {
        cleanup();

        ConfFileTemp conf_file_remote;
        {
            const std::string config_remote = R"(
allow_anonymous true
log_debug false

listen {
    protocol mqtt
    port 51883
})";
            conf_file_remote.writeLine(formatString(config_remote, protocol_version.c_str()));
            conf_file_remote.closeFile();
        }

        std::vector<std::string> args_remote {"--config-file", conf_file_remote.getFilePath()};

        MainAppAsFork remoteServer(args_remote);
        remoteServer.start();
        remoteServer.waitForStarted(51883);

        FlashMQTestClient clientToRemote;
        clientToRemote.start();
        clientToRemote.connectClient(ProtocolVersion::Mqtt5, 51883);

        {
            Publish pub("ManglerRemote/shoes/retainme", "asdf", 2);
            pub.retain = true;
            clientToRemote.publish(pub);
        }

        ConfFileTemp conf_file_local;
        {
            const std::string config_local = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 51883
    subscribe ManglerRemote/shoes/# 2
    publish ManglerLocal/boots/# 2
    clientid_prefix Mangler
    local_prefix ManglerLocal/
    remote_prefix ManglerRemote/
    protocol_version %s
}
listen {
    protocol mqtt
    port 21883
})";
            conf_file_local.writeLine(formatString(config_local, protocol_version.c_str()));
            conf_file_local.closeFile();

            std::vector<std::string> args_local {"--config-file", conf_file_local.getFilePath()};

            // We consider our normal test client as 'local'
            init(args_local);
        }

        FlashMQTestClient clientToLocal;
        clientToLocal.start();
        clientToLocal.connectClient(ProtocolVersion::Mqtt5);
        clientToLocal.subscribe("#", 1);
        clientToLocal.waitForMessageCount(1);

        {
            auto ro = clientToLocal.receivedObjects.lock();

            FMQ_COMPARE(ro->receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerLocal/shoes/retainme"));
        }

        clientToLocal.clearReceivedLists();

        waitForMessagesOverBridge(clientToRemote, clientToLocal, "ManglerRemote/shoes/connectiontest");

        {
            auto ro = clientToLocal.receivedObjects.lock();

            FMQ_COMPARE(ro->receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerLocal/shoes/connectiontest"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), std::string("connectiontest"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getRetain(), false);
        }

        {
            FlashMQTestClient clientToLocal;
            clientToLocal.start();

            clientToLocal.connectClient(ProtocolVersion::Mqtt5);
            clientToLocal.subscribe("#", 1);

            clientToLocal.waitForPacketCount(1);
            auto ro = clientToLocal.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerLocal/shoes/retainme"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "asdf");
            FMQ_COMPARE(ro->receivedPublishes.at(0).getRetain(), true);
        }

        // Push a retained message to local
        {
            FlashMQTestClient clientToLocal;
            clientToLocal.start();
            clientToLocal.connectClient(ProtocolVersion::Mqtt5);
            Publish pub("ManglerLocal/boots/retainmetoo", "zuOJHOekvdkGD9FH", 2);
            pub.retain = true;
            clientToLocal.publish(pub);
        }

        // That we then must see as retained on the remote
        {
            FlashMQTestClient clientToRemote;
            clientToRemote.start();

            clientToRemote.connectClient(ProtocolVersion::Mqtt5, 51883);
            clientToRemote.subscribe("ManglerRemote/boots/#", 2);

            clientToRemote.waitForMessageCount(1);
            auto ro = clientToRemote.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerRemote/boots/retainmetoo"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 2);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "zuOJHOekvdkGD9FH");
            FMQ_COMPARE(ro->receivedPublishes.at(0).getRetain(), true);
        }
    }
}

/**
 * @brief Test if queued QoS messages have have their prefixes applied. Special care is taken in FlashMQ to support
 * that (the topic_override is part of the QueuedPublish), so we need to test it.
 */
void MainTests::forkingTestBridgeWithLocalAndRemotePrefixQueuedQoS()
{
    for (const std::string protocol_version : {"mqtt5", "mqtt3.1.1"})
    {
        cleanup();

        ConfFileTemp conf_file_local;
        const std::string config_local = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 21883
    subscribe ManglerRemote/shoes/# 2
    publish ManglerLocal/boots/# 2
    clientid_prefix Mangler
    remote_clean_start false
    local_clean_start false
    remote_session_expiry_interval 300
    local_session_expiry_interval 300
    local_prefix ManglerLocal/
    remote_prefix ManglerRemote/
    protocol_version %s
}
listen {
    protocol mqtt
    port 51883
})";
        conf_file_local.writeLine(formatString(config_local, protocol_version.c_str()));
        conf_file_local.closeFile();

        std::vector<std::string> args_local {"--config-file", conf_file_local.getFilePath()};

        MainAppAsFork localServer(args_local);
        localServer.start();
        localServer.waitForStarted(51883);

        FlashMQTestClient clientToLocalWithBridge;
        clientToLocalWithBridge.start();
        clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt5, 51883);

        FlashMQTempDir remote_server_storage_dir;

        ConfFileTemp conf_file_remote;
        const std::string config_remote = R"(
allow_anonymous true
log_debug false
storage_dir %s

listen {
    protocol mqtt
    port 21883
}
)";
        conf_file_remote.writeLine(formatString(config_remote, remote_server_storage_dir.getPath().c_str()));
        conf_file_remote.closeFile();
        std::vector<std::string> args_remote {"--config-file", conf_file_remote.getFilePath()};

        // Bring the remote on-line
        init(args_remote);

        FlashMQTestClient clientToRemote;
        clientToRemote.start();

        clientToRemote.connectClient(ProtocolVersion::Mqtt5);
        clientToRemote.subscribe("#", 1);

        waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "ManglerLocal/boots/connectiontest");

        {
            auto ro = clientToRemote.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerRemote/boots/connectiontest"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), std::string("connectiontest"));
        }

        clientToRemote.clearReceivedLists();

        // Stop the remote
        cleanup();

        Publish pub("ManglerLocal/boots/queue_me", "late to the party", 1);
        clientToLocalWithBridge.publish(pub);

        std::cout << "Starting remote server again" << std::endl;

        // Start the remote again
        init(args_remote);

        {
            FlashMQTestClient clientToRemote;
            clientToRemote.start();

            clientToRemote.connectClient(ProtocolVersion::Mqtt5, false, 1000, [](Connect &c) {
                c.clientid = "QueuedReceiver_666";
            });
            clientToRemote.subscribe("+/boots/queue_me", 1);

            clientToRemote.waitForMessageCount(1, 10);
            auto ro = clientToRemote.receivedObjects.lock();
            FMQ_COMPARE(ro->receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getTopic(), std::string("ManglerRemote/boots/queue_me"));
            FMQ_COMPARE(ro->receivedPublishes.at(0).getQos(), 1);
            FMQ_COMPARE(ro->receivedPublishes.at(0).getPayloadView(), "late to the party");
        }
    }

}












