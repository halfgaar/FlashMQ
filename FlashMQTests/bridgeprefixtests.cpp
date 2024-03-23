#include "maintests.h"
#include "conffiletemp.h"
#include "flashmqtestclient.h"
#include "mainappasfork.h"
#include "testhelpers.h"

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
    for (const std::string &protocol_version : {"mqtt5", "mqtt3.1"})
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
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("ManglerRemote/boots"));
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getPayloadView(), "asdf");
        }

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToRemote.publish("ManglerRemote/shoes", "are made for walking", 2);
            clientToLocalWithBridge.waitForMessageCount(1);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getTopic(), std::string("ManglerLocal/shoes"));
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getPayloadView(), "are made for walking");
        }
    }
}

void MainTests::forkingTestBridgeWithOnlyRemotePrefix()
{
    for (const std::string &protocol_version : {"mqtt5", "mqtt3.1"})
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
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("ManglerRemote/boots"));
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getPayloadView(), "asdf");
        }

        // Make sure other clients connected to the server with prefixes defined get normal topics. I.e. test packet cache.
        {
            receiverToLocal.waitForMessageCount(1);
            FMQ_COMPARE(receiverToLocal.receivedPublishes.front().getTopic(), std::string("boots"));
            FMQ_COMPARE(receiverToLocal.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(receiverToLocal.receivedPublishes.front().getPayloadView(), "asdf");

            receiverToLocal2.waitForMessageCount(1);
            FMQ_COMPARE(receiverToLocal2.receivedPublishes.back().getTopic(), std::string("boots"));
            FMQ_COMPARE(receiverToLocal2.receivedPublishes.back().getQos(), 1);
            FMQ_COMPARE(receiverToLocal2.receivedPublishes.back().getPayloadView(), "asdf");
        }

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToRemote.publish("ManglerRemote/shoes", "are made for walking", 2);
            clientToLocalWithBridge.waitForMessageCount(1);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getTopic(), std::string("shoes"));
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getPayloadView(), "are made for walking");
        }


    }
}

void MainTests::forkingTestBridgeWithOnlyLocalPrefix()
{
    for (const std::string &protocol_version : {"mqtt5", "mqtt3.1"})
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
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("boots"));
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getPayloadView(), "asdf");
        }

        clientToLocalWithBridge.clearReceivedLists();
        clientToRemote.clearReceivedLists();

        {
            clientToRemote.publish("shoes", "are made for walking", 2);
            clientToLocalWithBridge.waitForMessageCount(1);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getTopic(), std::string("ManglerLocal/shoes"));
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getPayloadView(), "are made for walking");
        }

        // Make sure other clients connected to the server with prefixes defined get normal topics.
        {
            clientToLocalWithBridge.clearReceivedLists();
            senderToLocal.connectClient(ProtocolVersion::Mqtt31, 51183);
            senderToLocal.publish("panic", "attack", 0);
            clientToLocalWithBridge.waitForMessageCount(1);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getTopic(), std::string("panic"));
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getQos(), 0);
            FMQ_COMPARE(clientToLocalWithBridge.receivedPublishes.front().getPayloadView(), "attack");
        }
    }
}

void MainTests::forkingTestBridgeWithLocalAndRemotePrefixRetained()
{
    for (const std::string &protocol_version : {"mqtt5", "mqtt3.1"})
    {
        cleanup();

        ConfFileTemp confFile;
        const std::string config = R"(
allow_anonymous true
log_debug false

bridge {
    address ::1
    port 21883
    subscribe ManglerRemote/shoes/# 2
    publish ManglerLocal/boots/# 2
    clientid_prefix Mangler
    local_prefix ManglerLocal/
    remote_prefix ManglerRemote/
    protocol_version %s
}
listen {
    protocol mqtt
    port 51883
})";
        confFile.writeLine(formatString(config, protocol_version.c_str()));
        confFile.closeFile();

        std::vector<std::string> args {"--config-file", confFile.getFilePath()};

        MainAppAsFork localServer(args);
        localServer.start();
        localServer.waitForStarted(51883);

        FlashMQTestClient clientToLocalWithBridge;
        clientToLocalWithBridge.start();
        clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt5, 51883);

        {
            Publish pub("ManglerLocal/boots/retainme", "asdf", 2);
            pub.retain = true;
            clientToLocalWithBridge.publish(pub);
        }

        // Bring the remote on-line
        init();

        FlashMQTestClient clientToRemote;
        clientToRemote.start();

        clientToRemote.connectClient(ProtocolVersion::Mqtt5);
        clientToRemote.subscribe("#", 1);

        clientToRemote.waitForMessageCount(1);
        FMQ_COMPARE(clientToRemote.receivedPublishes.size(), static_cast<size_t>(1));
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("ManglerRemote/boots/retainme"));

        clientToRemote.clearReceivedLists();

        waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "ManglerLocal/boots/connectiontest");

        FMQ_COMPARE(clientToRemote.receivedPublishes.size(), static_cast<size_t>(1));
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("ManglerRemote/boots/connectiontest"));
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getQos(), 1);
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getPayloadView(), std::string("connectiontest"));
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getRetain(), false);

        {
            FlashMQTestClient clientToRemote;
            clientToRemote.start();

            clientToRemote.connectClient(ProtocolVersion::Mqtt5);
            clientToRemote.subscribe("#", 1);

            clientToRemote.waitForPacketCount(1);
            FMQ_COMPARE(clientToRemote.receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("ManglerRemote/boots/retainme"));
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getPayloadView(), "asdf");
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getRetain(), true);
        }
    }
}

/**
 * @brief Test if queued QoS messages have have their prefixes applied. Special care is taken in FlashMQ to support
 * that (the topic_override is part of the QueuedPublish), so we need to test it.
 */
void MainTests::forkingTestBridgeWithLocalAndRemotePrefixQueuedQoS()
{
    for (const std::string &protocol_version : {"mqtt5", "mqtt3.1"})
    {
        cleanup();

        ConfFileTemp confFile;
        const std::string config = R"(
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
        confFile.writeLine(formatString(config, protocol_version.c_str()));
        confFile.closeFile();

        std::vector<std::string> args {"--config-file", confFile.getFilePath()};

        MainAppAsFork localServer(args);
        localServer.start();
        localServer.waitForStarted(51883);

        FlashMQTestClient clientToLocalWithBridge;
        clientToLocalWithBridge.start();
        clientToLocalWithBridge.connectClient(ProtocolVersion::Mqtt5, 51883);

        // Bring the remote on-line
        init();

        FlashMQTestClient clientToRemote;
        clientToRemote.start();

        clientToRemote.connectClient(ProtocolVersion::Mqtt5);
        clientToRemote.subscribe("#", 1);

        waitForMessagesOverBridge(clientToLocalWithBridge, clientToRemote, "ManglerLocal/boots/connectiontest");

        FMQ_COMPARE(clientToRemote.receivedPublishes.size(), static_cast<size_t>(1));
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("ManglerRemote/boots/connectiontest"));
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getQos(), 1);
        FMQ_COMPARE(clientToRemote.receivedPublishes.front().getPayloadView(), std::string("connectiontest"));

        clientToRemote.clearReceivedLists();

        // Stop the remote
        cleanup();

        Publish pub("ManglerLocal/boots/queue_me", "late to the party", 1);
        clientToLocalWithBridge.publish(pub);

        std::cout << "Starting remote server again" << std::endl;

        // Start the remote again
        init();

        {
            FlashMQTestClient clientToRemote;
            clientToRemote.start();

            clientToRemote.connectClient(ProtocolVersion::Mqtt5, false, 1000, [](Connect &c) {
                c.clientid = "QueuedReceiver_666";
            });
            clientToRemote.subscribe("+/boots/queue_me", 1);

            clientToRemote.waitForMessageCount(1, 10);
            FMQ_COMPARE(clientToRemote.receivedPublishes.size(), static_cast<size_t>(1));
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getTopic(), std::string("ManglerRemote/boots/queue_me"));
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getQos(), 1);
            FMQ_COMPARE(clientToRemote.receivedPublishes.front().getPayloadView(), "late to the party");
        }
    }

}












