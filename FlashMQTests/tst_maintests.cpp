/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include <list>
#include <unordered_map>
#include <sys/sysinfo.h>
#include <fstream>

#include "maintests.h"
#include "testhelpers.h"
#include "flashmqtestclient.h"
#include "conffiletemp.h"

#include "threadglobals.h"
#include "threadlocalutils.h"
#include "retainedmessagesdb.h"
#include "utils.h"

void MainTests::test_circbuf()
{
    CirBuf buf(64);

    MYCASTCOMPARE(buf.freeSpace(), 63);

    uint write_n = 40;

    char *head = buf.headPtr();
    for (uint i = 0; i < write_n; i++)
    {
        head[i] = i+1;
    }

    buf.advanceHead(write_n);

    QCOMPARE(buf.head, write_n);
    MYCASTCOMPARE(buf.tail, 0);
    QCOMPARE(buf.maxReadSize(), write_n);
    QCOMPARE(buf.maxWriteSize(), (64 - write_n - 1));
    QCOMPARE(buf.freeSpace(), 64 - write_n - 1);

    for (uint i = 0; i < write_n; i++)
    {
        MYCASTCOMPARE(buf.tailPtr()[i], i+1);
    }

    buf.advanceTail(write_n);
    QVERIFY(buf.tail == buf.head);
    QCOMPARE(buf.tail, write_n);
    MYCASTCOMPARE(buf.maxReadSize(), 0);
    QCOMPARE(buf.maxWriteSize(), (64 - write_n)); // no longer -1, because the head can point to 0 afterwards
    MYCASTCOMPARE(buf.freeSpace(), 63);

    write_n = buf.maxWriteSize();

    head = buf.headPtr();
    for (uint i = 0; i < write_n; i++)
    {
        head[i] = i+1;
    }
    buf.advanceHead(write_n);

    MYCASTCOMPARE(buf.head, 0);

    // Now write more, starting at the beginning.

    write_n = buf.maxWriteSize();

    head = buf.headPtr();
    for (uint i = 0; i < write_n; i++)
    {
        head[i] = i+100; // Offset by 100 so we can see if we overwrite the tail
    }
    buf.advanceHead(write_n);

    QCOMPARE(buf.tailPtr()[0], 1); // Did we not overwrite the tail?
    QCOMPARE(buf.head, buf.tail - 1);

}



void MainTests::test_circbuf_unwrapped_doubling()
{
    CirBuf buf(64);

    int w = 63;

    char *head = buf.headPtr();
    for (int i = 0; i < w; i++)
    {
        head[i] = i+1;
    }
    buf.advanceHead(63);

    char *tail = buf.tailPtr();
    for (int i = 0; i < w; i++)
    {
        QCOMPARE(tail[i], i+1);
    }
    QCOMPARE(buf.buf[63], 0); // Vacant place, because of the circulerness.

    MYCASTCOMPARE(buf.head, 63);
    MYCASTCOMPARE(buf.freeSpace(), 0);

    buf.doubleSize();
    tail = buf.tailPtr();

    for (int i = 0; i < w; i++)
    {
        QCOMPARE(tail[i], i+1);
    }

    for (int i = 63; i < 127; i++)
    {
        QCOMPARE(tail[i], 5);
    }

    QCOMPARE(tail[127], 68);

    MYCASTCOMPARE(buf.tail, 0);
    MYCASTCOMPARE(buf.head, 63);
    MYCASTCOMPARE(buf.maxWriteSize(), 64);
    MYCASTCOMPARE(buf.maxReadSize(), 63);
}

void MainTests::test_circbuf_wrapped_doubling()
{
    CirBuf buf(64);

    int w = 40;

    char *head = buf.headPtr();
    for (int i = 0; i < w; i++)
    {
        head[i] = i+1;
    }
    buf.advanceHead(w);

    MYCASTCOMPARE(buf.tail, 0);
    MYCASTCOMPARE(buf.head, w);
    MYCASTCOMPARE(buf.maxReadSize(), 40);
    MYCASTCOMPARE(buf.maxWriteSize(), 23);

    buf.advanceTail(40);

    MYCASTCOMPARE(buf.maxWriteSize(), 24);

    head = buf.headPtr();
    for (int i = 0; i < 24; i++)
    {
        head[i] = 99;
    }
    buf.advanceHead(24);

    MYCASTCOMPARE(buf.tail, 40);
    MYCASTCOMPARE(buf.head, 0);
    MYCASTCOMPARE(buf.maxReadSize(), 24);
    MYCASTCOMPARE(buf.maxWriteSize(), 39);

    // Now write a little more, which starts at the start

    head = buf.headPtr();
    for (int i = 0; i < 10; i++)
    {
        head[i] = 88;
    }
    buf.advanceHead(10);
    MYCASTCOMPARE(buf.head, 10);

    buf.doubleSize();

    // The 88's that were appended at the start, should now appear at the end;
    for (int i = 64; i < 74; i++)
    {
        MYCASTCOMPARE(buf.buf[i], 88);
    }

    MYCASTCOMPARE(buf.tail, 40);
    MYCASTCOMPARE(buf.head, 74);
}

void MainTests::test_circbuf_full_wrapped_buffer_doubling()
{
    CirBuf buf(64);

    buf.head = 10;
    buf.tail = 10;

    memset(buf.headPtr(), 1, buf.maxWriteSize());
    buf.advanceHead(buf.maxWriteSize());
    memset(buf.headPtr(), 2, buf.maxWriteSize());
    buf.advanceHead(buf.maxWriteSize());

    for (int i = 0; i < 9; i++)
    {
        QCOMPARE(buf.buf[i], 2);
    }

    QCOMPARE(buf.buf[9], 0);

    for (int i = 10; i < 64; i++)
    {
        QCOMPARE(buf.buf[i], 1);
    }

    QVERIFY(true);

    buf.doubleSize();

    // The places where value was 1 are the same
    for (int i = 10; i < 64; i++)
    {
        QCOMPARE(buf.buf[i], 1);
    }

    // The nine 2's have been moved to the end
    for (int i = 64; i < 73; i++)
    {
        QCOMPARE(buf.buf[i], 2);
    }

    // The rest are our debug 5.
    for (int i = 73; i < 128; i++)
    {
        QCOMPARE(buf.buf[i], 5);
    }

    QVERIFY(true);
}

void MainTests::test_validSubscribePath()
{
    QVERIFY(isValidSubscribePath("one/two/three"));
    QVERIFY(isValidSubscribePath("one//three"));
    QVERIFY(isValidSubscribePath("one/+/three"));
    QVERIFY(isValidSubscribePath("one/+/#"));
    QVERIFY(isValidSubscribePath("#"));
    QVERIFY(isValidSubscribePath("///"));
    QVERIFY(isValidSubscribePath("//#"));
    QVERIFY(isValidSubscribePath("+"));
    QVERIFY(isValidSubscribePath(""));
    QVERIFY(isValidSubscribePath("hello"));

    QVERIFY(isValidSubscribePath("$SYS/hello"));
    QVERIFY(isValidSubscribePath("hello/$SYS")); // Hmm, is this valid?

    QVERIFY(!isValidSubscribePath("one/tw+o/three"));
    QVERIFY(!isValidSubscribePath("one/+o/three"));
    QVERIFY(!isValidSubscribePath("one/a+/three"));
    QVERIFY(!isValidSubscribePath("#//three"));
    QVERIFY(!isValidSubscribePath("#//+"));
    QVERIFY(!isValidSubscribePath("one/#/+"));
}

void MainTests::test_various_packet_sizes()
{
    std::vector<ProtocolVersion> protocols {ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5};
    std::list<std::string> payloads {std::string(8000,3), std::string(10*1024*1024, 5)};

    for (const ProtocolVersion senderVersion : protocols)
    {
        for (const ProtocolVersion receiverVersion : protocols)
        {
            for (const std::string &payload : payloads)
            {
                FlashMQTestClient sender;
                FlashMQTestClient receiver;

                std::string topic = "hugepacket";

                sender.start();
                sender.connectClient(senderVersion);

                receiver.start();
                receiver.connectClient(receiverVersion);
                receiver.subscribe(topic, 0);

                sender.publish(topic, payload, 0);
                receiver.waitForMessageCount(1, 2);

                MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);

                MqttPacket &msg = receiver.receivedPublishes.front();
                QCOMPARE(msg.getPayloadCopy(), payload);
                QVERIFY(!msg.getRetain());
            }
        }
    }
}

void MainTests::test_acl_tree()
{
    AclTree aclTree;

    aclTree.addTopic("one/two/#", AclGrant::ReadWrite, AclTopicType::Strings);
    aclTree.addTopic("one/two/three", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("a/+/c", AclGrant::Read, AclTopicType::Strings);
    aclTree.addTopic("1/+/3", AclGrant::ReadWrite, AclTopicType::Strings);
    aclTree.addTopic("1/blocked/3", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("cat/+/dog", AclGrant::Write, AclTopicType::Strings);
    aclTree.addTopic("cat/blocked/dog", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("cat/blocked/dog/bla/bla/#", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("cat/turtle/dog/%u/bla/#", AclGrant::ReadWrite, AclTopicType::Strings);
    aclTree.addTopic("fish/turtle/dog/%u/bla/#", AclGrant::ReadWrite, AclTopicType::Strings, "john");
    aclTree.addTopic("fish/turtle/dog/%u/bla/#", AclGrant::ReadWrite, AclTopicType::Strings, "AAA");

    QCOMPARE(aclTree.findPermission(splitToVector("one/two", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/four", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/four/five/six", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/four/five/six", '/'), AclGrant::Write, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/three", '/'), AclGrant::Read, "", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("asdf", '/'), AclGrant::Read, "", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/b/c", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("a/b/c", '/'), AclGrant::Write, "", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/wildcardmatch/c", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("1/2/3", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("1/2/3", '/'), AclGrant::Write, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("1/wildcardmatch/3", '/'), AclGrant::Write, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("1/wildcardmatch/3", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/2/dog", '/'), AclGrant::Write, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/2/dog", '/'), AclGrant::Read, "", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/blocked/dog", '/'), AclGrant::Write, "", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/blocked/dog", '/'), AclGrant::Read, "", "clientid"), AuthResult::acl_denied);

    // Test that wildcards aren't replaced here
    QCOMPARE(aclTree.findPermission(splitToVector("cat/turtle/dog/%u/bla/sdf", '/'), AclGrant::Read, "", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("fish/turtle/dog/%u/bla/sdf", '/'), AclGrant::Read, "john", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("fish/turtle/dog/john/bla/sdf", '/'), AclGrant::Read, "john", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("fish/turtle/dog/AAA/bla/sdf", '/'), AclGrant::Read, "AAA", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("fish/turtle/dog/john/bla/sdf", '/'), AclGrant::Read, "john", "clientid"), AuthResult::acl_denied);

}

void MainTests::test_acl_tree2()
{
    AclTree aclTree;

    aclTree.addTopic("one/two/#", AclGrant::ReadWrite, AclTopicType::Strings);
    aclTree.addTopic("one/two/three", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("one/two/three", AclGrant::ReadWrite, AclTopicType::Strings, "Metusalem");
    aclTree.addTopic("a/+/c", AclGrant::Read, AclTopicType::Strings);
    aclTree.addTopic("1/+/3", AclGrant::ReadWrite, AclTopicType::Strings);
    aclTree.addTopic("1/blocked/3", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("cat/+/dog", AclGrant::Write, AclTopicType::Strings);
    aclTree.addTopic("cat/blocked/dog", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("cat/blocked/dog/bla/bla/#", AclGrant::Deny, AclTopicType::Strings);

    // Test all these with a user, which should be denied.
    QCOMPARE(aclTree.findPermission(splitToVector("one/two", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/four", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/four/five/six", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/four/five/six", '/'), AclGrant::Write, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("one/two/three", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("asdf", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/b/c", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/b/c", '/'), AclGrant::Write, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/wildcardmatch/c", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("1/2/3", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("1/2/3", '/'), AclGrant::Write, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("1/wildcardmatch/3", '/'), AclGrant::Write, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("1/wildcardmatch/3", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/2/dog", '/'), AclGrant::Write, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/2/dog", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/blocked/dog", '/'), AclGrant::Write, "a", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("cat/blocked/dog", '/'), AclGrant::Read, "a", "clientid"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("one/two/three", '/'), AclGrant::Read, "Metusalem", "clientid"), AuthResult::success);
}

void MainTests::test_acl_patterns_username()
{
    AclTree aclTree;

    aclTree.addTopic("one/%u/three", AclGrant::ReadWrite, AclTopicType::Patterns);
    aclTree.addTopic("a/%u/c", AclGrant::Read, AclTopicType::Patterns);
    aclTree.addTopic("d/%u/f/#", AclGrant::Read, AclTopicType::Patterns);
    aclTree.addTopic("one/Jheronimus/three", AclGrant::Deny, AclTopicType::Strings);
    aclTree.addTopic("one/santaclause/three", AclGrant::Deny, AclTopicType::Strings, "santaclause");
    aclTree.addTopic("%u/#", AclGrant::ReadWrite, AclTopicType::Patterns);

    // Succeeds, because the anonymous deny should have no effect on the authenticated ACL check, so it checks the pattern based.
    QCOMPARE(aclTree.findPermission(splitToVector("one/Jheronimus/three", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);

    // The fixed-strings deny for 'santaclause' should override the pattern based ReadWrite.
    QCOMPARE(aclTree.findPermission(splitToVector("one/santaclause/three", '/'), AclGrant::Read, "santaclause", "clientid"), AuthResult::acl_denied);

    aclTree.addTopic("some/thing", AclGrant::ReadWrite, AclTopicType::Strings, "Rembrandt");
    aclTree.addTopic("some/thing", AclGrant::ReadWrite, AclTopicType::Patterns);

    QCOMPARE(aclTree.findPermission(splitToVector("one/Jheronimus/three", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/Theo/three", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("a/Jheronimus/c", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("a/NotJheronimus/c", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/Jheronimus/c", '/'), AclGrant::Write, "Jheronimus", "clientid"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("d/Jheronimus/f", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Jheronimus/f/A", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Jheronimus/f/A/B", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);

    // Repeat the test, but now with a user for which there is also an unrelated user specific ACL.

    QCOMPARE(aclTree.findPermission(splitToVector("one/Rembrandt/three", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/Theo/three", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("a/Rembrandt/c", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("a/NotRembrandt/c", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/Rembrandt/c", '/'), AclGrant::Write, "Rembrandt", "clientid"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("d/Rembrandt/f", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Rembrandt/f/A", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Rembrandt/f/A/B", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);

    QCOMPARE(aclTree.findPermission(splitToVector("Rembrandt", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("Rembrandt", '/'), AclGrant::Write, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("Rembrandt/wer", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("Rembrandt/eee", '/'), AclGrant::Write, "Rembrandt", "clientid"), AuthResult::success);

}

void MainTests::test_acl_patterns_clientid()
{
    AclTree aclTree;

    aclTree.addTopic("one/%c/three", AclGrant::ReadWrite, AclTopicType::Patterns);
    aclTree.addTopic("a/%c/c", AclGrant::Read, AclTopicType::Patterns);
    aclTree.addTopic("d/%c/f/#", AclGrant::Read, AclTopicType::Patterns);

    QCOMPARE(aclTree.findPermission(splitToVector("one/clientid_one/three", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/clientid_two/three", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("a/clientid_one/c", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("a/not_clientidone/c", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/clientid_one/c", '/'), AclGrant::Write, "foo", "clientid_one"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("d/clientid_one/f", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/clientid_one/f/A", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/clientid_one/f/A/B", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::success);
}

/**
 * @brief MainTests::test_loading_acl_file was created because assertions in it failed when publishing $SYS topics were passed through the ACL
 * layer. That's why it seemingly doesn't do anything.
 */
void MainTests::test_loading_acl_file()
{
    ConfFileTemp aclFile;
    aclFile.writeLine("topic readwrite one/two");
    aclFile.closeFile();

    ConfFileTemp confFile;
    confFile.writeLine("mosquitto_acl_file " + aclFile.getFilePath());
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    usleep(1000000);

    QVERIFY(true);
}

#ifndef FMQ_NO_SSE
void MainTests::test_sse_split()
{
    SimdUtils data;

    std::list<std::string> topics;
    topics.push_back("one/two/threeabcasdfasdf/koe");
    topics.push_back("/two/threeabcasdfasdf/koe"); // Test empty component.
    topics.push_back("//two/threeabcasdfasdf/koe"); // Test two empty components.
    topics.push_back("//1234567890abcde/bla/koe"); // Test two empty components, 15 char topic (one byte short of 16 alignment).
    topics.push_back("//1234567890abcdef/bla/koe"); // Test two empty components, 16 char topic
    topics.push_back("//1234567890abcdefg/bla/koe"); // Test two empty components, 17 char topic
    topics.push_back("//1234567890abcdefg/1234567890abcdefg/koe"); // Test two empty components, two 17 char topics
    topics.push_back("//1234567890abcdef/1234567890abcdefg/koe"); // Test two empty components, 16 and 17 char
    topics.push_back("//1234567890abcdef/1234567890abcdefg/koe/");
    topics.push_back("//1234567890abcdef/1234567890abcdefg/koe//");
    topics.push_back("//1234567890abcdef/1234567890abcdef/");
    topics.push_back("/");
    topics.push_back("");

    for (const std::string &t : topics)
    {
        std::vector<std::string> output = data.splitTopic(t);
        QCOMPARE(output, splitToVector(t, '/'));
    }
}
#endif

void MainTests::test_validUtf8Generic()
{
    char m[16];

    QVERIFY(isValidUtf8Generic(""));
    QVERIFY(isValidUtf8Generic("ƀ"));
    QVERIFY(isValidUtf8Generic("Hello"));

    std::memset(m, 0, 16);
    QVERIFY(!isValidUtf8Generic(std::string(m, 16)));

    QVERIFY(isValidUtf8Generic("Straƀe")); // two byte chars
    QVERIFY(isValidUtf8Generic("StraƀeHelloHelloHelloHelloHelloHello")); // two byte chars
    QVERIFY(isValidUtf8Generic("HelloHelloHelloHelloHelloHelloHelloHelloStraƀeHelloHelloHelloHelloHelloHello")); // two byte chars

    std::memset(m, 0, 16);
    m[0] = 'a';
    m[1] = 13; // is \r
    QVERIFY(!isValidUtf8Generic(std::string(m, 16)));

    const std::string unicode_ballet_shoes("🩰");
    QVERIFY(unicode_ballet_shoes.length() == 4);
    QVERIFY(isValidUtf8Generic(unicode_ballet_shoes));

    const std::string unicode_ballot_box("☐");
    QVERIFY(unicode_ballot_box.length() == 3);
    QVERIFY(isValidUtf8Generic(unicode_ballot_box));

    std::memset(m, 0, 16);
    m[0] = 0b11000001; // Start 2 byte char
    m[1] = 0b00000001; // Next byte doesn't start with 1, which is wrong
    std::string a(m, 2);
    QVERIFY(!isValidUtf8Generic(a));

    std::memset(m, 0, 16);
    m[0] = 0b11100001; // Start 3 byte char
    m[1] = 0b10100001;
    m[2] = 0b00000001; // Next byte doesn't start with 1, which is wrong
    std::string b(m, 3);
    QVERIFY(!isValidUtf8Generic(b));

    std::memset(m, 0, 16);
    m[0] = 0b11110001; // Start 4 byte char
    m[1] = 0b10100001;
    m[2] = 0b10100001;
    m[3] = 0b00000001; // Next byte doesn't start with 1, which is wrong
    std::string c(m, 4);
    QVERIFY(!isValidUtf8Generic(c));

    std::memset(m, 0, 16);
    m[0] = 0b11110001; // Start 4 byte char
    m[1] = 0b10100001;
    m[2] = 0b00100001; // Doesn't start with 1: invalid.
    m[3] = 0b10000001;
    std::string d(m, 4);
    QVERIFY(!isValidUtf8Generic(d));

    // Upper ASCII, invalid
    std::memset(m, 0, 16);
    m[0] = 127;
    std::string e(m, 1);
    QVERIFY(!isValidUtf8Generic(e));
}

#ifndef FMQ_NO_SSE
void MainTests::test_validUtf8Sse()
{
    SimdUtils data;

    char m[16];

    QVERIFY(data.isValidUtf8(""));
    QVERIFY(data.isValidUtf8("ƀ"));
    QVERIFY(data.isValidUtf8("Hello"));

    std::memset(m, 0, 16);
    QVERIFY(!data.isValidUtf8(std::string(m, 16)));

    QVERIFY(data.isValidUtf8("Straƀe")); // two byte chars
    QVERIFY(data.isValidUtf8("StraƀeHelloHelloHelloHelloHelloHello")); // two byte chars
    QVERIFY(data.isValidUtf8("HelloHelloHelloHelloHelloHelloHelloHelloStraƀeHelloHelloHelloHelloHelloHello")); // two byte chars

    QVERIFY(!data.isValidUtf8("Straƀe#", true));
    QVERIFY(!data.isValidUtf8("ƀ#", true));
    QVERIFY(!data.isValidUtf8("#ƀ", true));
    QVERIFY(!data.isValidUtf8("+", true));
    QVERIFY(!data.isValidUtf8("🩰+asdfasdfasdf", true));
    QVERIFY(!data.isValidUtf8("+asdfasdfasdf", true));

    std::memset(m, 0, 16);
    m[0] = 'a';
    m[1] = 13; // is \r
    QVERIFY(!data.isValidUtf8(std::string(m, 16)));

    const std::string unicode_ballet_shoes("🩰");
    QVERIFY(unicode_ballet_shoes.length() == 4);
    QVERIFY(data.isValidUtf8(unicode_ballet_shoes));

    const std::string unicode_ballot_box("☐");
    QVERIFY(unicode_ballot_box.length() == 3);
    QVERIFY(data.isValidUtf8(unicode_ballot_box));

    std::memset(m, 0, 16);
    m[0] = 0b11000001; // Start 2 byte char
    m[1] = 0b00000001; // Next byte doesn't start with 1, which is wrong
    std::string a(m, 2);
    QVERIFY(!data.isValidUtf8(a));

    std::memset(m, 0, 16);
    m[0] = 0b11100001; // Start 3 byte char
    m[1] = 0b10100001;
    m[2] = 0b00000001; // Next byte doesn't start with 1, which is wrong
    std::string b(m, 3);
    QVERIFY(!data.isValidUtf8(b));

    std::memset(m, 0, 16);
    m[0] = 0b11110001; // Start 4 byte char
    m[1] = 0b10100001;
    m[2] = 0b10100001;
    m[3] = 0b00000001; // Next byte doesn't start with 1, which is wrong
    std::string c(m, 4);
    QVERIFY(!data.isValidUtf8(c));

    std::memset(m, 0, 16);
    m[0] = 0b11110001; // Start 4 byte char
    m[1] = 0b10100001;
    m[2] = 0b00100001; // Doesn't start with 1: invalid.
    m[3] = 0b10000001;
    std::string d(m, 4);
    QVERIFY(!data.isValidUtf8(d));

    // Upper ASCII, invalid
    std::memset(m, 0, 16);
    m[0] = 127;
    std::string e(m, 1);
    QVERIFY(!data.isValidUtf8(e));
}

/**
 * @brief MainTests::test_utf8_nonchars tests the 66 non-chars in unicode, and a bit around them.
 */
void MainTests::test_utf8_nonchars()
{
    SimdUtils simd_utils;

    for (int i = 0x80; i < 0x90; i++)
    {
        std::string c;
        c.push_back(0xEF);
        c.push_back(0xB7);
        c.push_back(i);

        QVERIFY(isValidUtf8Generic(c));
        QVERIFY(simd_utils.isValidUtf8(c));
    }

    // The invalid ones
    for (int i = 0x90; i <= 0xAF; i++)
    {
        std::string c;
        c.push_back(0xEF);
        c.push_back(0xB7);
        c.push_back(i);

        QVERIFY(!isValidUtf8Generic(c));
        QVERIFY(!simd_utils.isValidUtf8(c));
    }

    for (int i = 0xB0; i < 0xB5; i++)
    {
        std::string c;
        c.push_back(0xEF);
        c.push_back(0xB7);
        c.push_back(i);

        QVERIFY(isValidUtf8Generic(c));
        QVERIFY(simd_utils.isValidUtf8(c));
    }

    // Now the last two code points of the multilingual planes

    {
        std::string s;
        s.clear(); s.push_back(0xEF); s.push_back(0xBF); s.push_back(0xBE);
        QVERIFY(!isValidUtf8Generic(s));
        QVERIFY(!simd_utils.isValidUtf8(s));

        s.clear(); s.push_back(0xEF); s.push_back(0xBF); s.push_back(0xBF);
        QVERIFY(!isValidUtf8Generic(s));
        QVERIFY(!simd_utils.isValidUtf8(s));

        // Adjacent one that is valid.
        s.clear(); s.push_back(0xEF); s.push_back(0xBF); s.push_back(0xBD);
        QVERIFY(isValidUtf8Generic(s));
        QVERIFY(simd_utils.isValidUtf8(s));
    }

    {
        std::string s;
        s.clear(); s.push_back(0xF0); s.push_back(0x9F); s.push_back(0xBF); s.push_back(0xBE);
        QVERIFY(!isValidUtf8Generic(s));
        QVERIFY(!simd_utils.isValidUtf8(s));

        s.clear(); s.push_back(0xF0); s.push_back(0x9F); s.push_back(0xBF); s.push_back(0xBF);
        QVERIFY(!isValidUtf8Generic(s));
        QVERIFY(!simd_utils.isValidUtf8(s));

        // Adjacent one that is valid.
        s.clear(); s.push_back(0xF0); s.push_back(0x9F); s.push_back(0xBF); s.push_back(0xBD);
        QVERIFY(isValidUtf8Generic(s));
        QVERIFY(simd_utils.isValidUtf8(s));
    }

    // TODO: there are more planes to check, but programming that out means encoding in UTF8.
}

/**
 * @brief MainTests::test_utf8_overlong tests multiple representations of '/' (ASCII 0xAF).
 *
 * U+002F = c0 af
 * U+002F = e0 80 af
 * U+002F = f0 80 80 af
 */
void MainTests::test_utf8_overlong()
{
    SimdUtils simd_utils;

    {
        std::string two;
        two.push_back(0xc0);
        two.push_back(0xaf);
        QVERIFY(!isValidUtf8Generic(two));
        QVERIFY(!simd_utils.isValidUtf8(two));
    }

    {
        std::string three;
        three.push_back(0xe0);
        three.push_back(0x80);
        three.push_back(0xaf);
        QVERIFY(!isValidUtf8Generic(three));
        QVERIFY(!simd_utils.isValidUtf8(three));
    }

    {
        std::string four;
        four.push_back(0xf0);
        four.push_back(0x80);
        four.push_back(0x80);
        four.push_back(0xaf);
        QVERIFY(!isValidUtf8Generic(four));
        QVERIFY(!simd_utils.isValidUtf8(four));
    }
}

void MainTests::test_utf8_compare_implementation()
{
    SimdUtils simd_utils;

    // Just something to look at. It prefixes lines with a red cross when the checker returns false. Note that this means you
    // don't see the difference between invalid UTF8 and valid but invalid for MQTT.
    std::ofstream outfile("/tmp/flashmq_utf8_test_result.txt", std::ios::binary);

    int line_count = 0;
    std::ifstream infile("UTF-8-test.txt", std::ios::binary);
    for(std::string line; getline(infile, line ); )
    {
        const bool a = isValidUtf8Generic(line);
        const bool b = simd_utils.isValidUtf8(line);

        QVERIFY(a == b);

        if (a)
            outfile << "\u2705 ";
        else
            outfile << "\u274C ";

        outfile << line << std::endl;

        line_count++;
    }

    QVERIFY(line_count > 40);
}
#endif

void MainTests::testPacketInt16Parse()
{
    std::vector<uint64_t> tests {128, 300, 64, 65550, 32000};

    for (const uint16_t id : tests)
    {
        Publish pub("hallo", "content", 1);
        MqttPacket packet(ProtocolVersion::Mqtt311, pub);
        packet.setPacketId(id);
        packet.pos -= 2;
        uint16_t idParsed = packet.readTwoBytesToUInt16();
        QVERIFY(id == idParsed);
    }
}

void MainTests::testRetainedMessageDB()
{
    try
    {
        std::string longpayload = getSecureRandomString(65537);
        std::string longTopic = formatString("one/two/%s", getSecureRandomString(4000).c_str());

        std::vector<RetainedMessage> messages;
        messages.emplace_back(Publish("one/two/three", "payload", 0));
        messages.emplace_back(Publish("one/two/wer", "payload", 1));
        messages.emplace_back(Publish("one/e/wer", "payload", 1));
        messages.emplace_back(Publish("one/wee/wer", "asdfasdfasdf", 1));
        messages.emplace_back(Publish("one/two/wer", "µsdf", 1));
        messages.emplace_back(Publish("/boe/bah", longpayload, 1));
        messages.emplace_back(Publish("one/two/wer", "paylasdfaoad", 1));
        messages.emplace_back(Publish("one/two/wer", "payload", 1));
        messages.emplace_back(Publish(longTopic, "payload", 1));
        messages.emplace_back(Publish(longTopic, longpayload, 1));
        messages.emplace_back(Publish("one", "µsdf", 1));
        messages.emplace_back(Publish("/boe", longpayload, 1));
        messages.emplace_back(Publish("one", "µsdf", 1));

        int clientidCount = 1;
        int usernameCount = 1;
        for (RetainedMessage &rm : messages)
        {
            rm.publish.client_id = formatString("Clientid__%d", clientidCount++);
            rm.publish.username = formatString("Username__%d", usernameCount++);
        }

        RetainedMessagesDB db("/tmp/flashmqtests_retained.db");
        db.openWrite();
        db.saveData(messages);
        db.closeFile();

        RetainedMessagesDB db2("/tmp/flashmqtests_retained.db");
        db2.openRead();
        std::list<RetainedMessage> messagesLoaded = db2.readData();
        db2.closeFile();

        QCOMPARE(messagesLoaded.size(), messages.size());

        auto itOrg = messages.begin();
        auto itLoaded = messagesLoaded.begin();
        while (itOrg != messages.end() && itLoaded != messagesLoaded.end())
        {
            RetainedMessage &one = *itOrg;
            RetainedMessage &two = *itLoaded;

            // Comparing the fields because the RetainedMessage class has an == operator that only looks at topic.
            QCOMPARE(one.publish.topic, two.publish.topic);
            QCOMPARE(one.publish.payload, two.publish.payload);
            QCOMPARE(one.publish.qos, two.publish.qos);

            QVERIFY(!two.publish.client_id.empty());
            QVERIFY(!two.publish.username.empty());
            QCOMPARE(two.publish.client_id, one.publish.client_id);
            QCOMPARE(two.publish.username, one.publish.username);

            itOrg++;
            itLoaded++;
        }
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}

void MainTests::testRetainedMessageDBNotPresent()
{
    try
    {
        RetainedMessagesDB db2("/tmp/flashmqtests_asdfasdfasdf.db");
        db2.openRead();
        std::list<RetainedMessage> messagesLoaded = db2.readData();
        db2.closeFile();

        MYCASTCOMPARE(messagesLoaded.size(), 0);

        QVERIFY2(false, "We should have run into an exception.");
    }
    catch (PersistenceFileCantBeOpened &ex)
    {
        QVERIFY(true);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}

void MainTests::testRetainedMessageDBEmptyList()
{
    try
    {
        std::vector<RetainedMessage> messages;

        RetainedMessagesDB db("/tmp/flashmqtests_retained.db");
        db.openWrite();
        db.saveData(messages);
        db.closeFile();

        RetainedMessagesDB db2("/tmp/flashmqtests_retained.db");
        db2.openRead();
        std::list<RetainedMessage> messagesLoaded = db2.readData();
        db2.closeFile();

        MYCASTCOMPARE(messages.size(), messagesLoaded.size());
        MYCASTCOMPARE(messages.size(), 0);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}

void MainTests::testSavingSessions()
{
    try
    {
        Settings settings;
        PluginLoader pluginLoader;
        std::shared_ptr<SubscriptionStore> store(new SubscriptionStore());
        std::shared_ptr<ThreadData> t(new ThreadData(0, settings, pluginLoader));

        // Kind of a hack...
        Authentication auth(settings);
        ThreadGlobals::assign(&auth);
        ThreadGlobals::assignThreadData(t.get());

        std::shared_ptr<Client> c1(new Client(0, t, nullptr, false, false, nullptr, settings, false));
        c1->setClientProperties(ProtocolVersion::Mqtt5, "c1", "user1", true, 60);
        store->registerClientAndKickExistingOne(c1, false, 512, 120);
        c1->getSession()->addIncomingQoS2MessageId(2);
        c1->getSession()->addIncomingQoS2MessageId(3);

        std::shared_ptr<Client> c2(new Client(0, t, nullptr, false, false, nullptr, settings, false));
        c2->setClientProperties(ProtocolVersion::Mqtt5, "c2", "user2", true, 60);
        store->registerClientAndKickExistingOne(c2, false, 512, 120);
        c2->getSession()->addOutgoingQoS2MessageId(55);
        c2->getSession()->addOutgoingQoS2MessageId(66);

        const std::string topic1 = "one/two/three";
        std::vector<std::string> subtopics;
        subtopics = splitTopic(topic1);
        store->addSubscription(c1, subtopics, 0, true, false);

        const std::string topic2 = "four/five/six";
        subtopics = splitTopic(topic2);
        store->addSubscription(c2, subtopics, 0, false, true);
        store->addSubscription(c1, subtopics, 0, false, false);

        const std::string topic3 = "";
        subtopics = splitTopic(topic3);
        store->addSubscription(c2, subtopics, 0, false, false);

        const std::string topic4 = "#";
        subtopics = splitTopic(topic4);
        store->addSubscription(c2, subtopics, 0, false, false);

        Publish publish("a/b/c", "Hello Barry", 1);
        publish.client_id = "ClientIdFromFakePublisher";
        publish.username = "UsernameFromFakePublisher";
        publish.setExpireAfter(10);

        usleep(1000000);

        std::shared_ptr<Session> c1ses = c1->getSession();
        c1.reset();
        MqttPacket publishPacket(ProtocolVersion::Mqtt5, publish);
        PublishCopyFactory fac(&publishPacket);
        c1ses->writePacket(fac, 1, false);

        store->saveSessionsAndSubscriptions("/tmp/flashmqtests_sessions.db");

        usleep(1000000);

        std::shared_ptr<SubscriptionStore> store2(new SubscriptionStore());
        store2->loadSessionsAndSubscriptions("/tmp/flashmqtests_sessions.db");

        MYCASTCOMPARE(store->sessionsById.size(), 2);
        MYCASTCOMPARE(store2->sessionsById.size(), 2);

        for (auto &pair : store->sessionsById)
        {
            std::shared_ptr<Session> &ses = pair.second;
            std::shared_ptr<Session> &ses2 = store2->sessionsById[pair.first];
            QCOMPARE(pair.first, ses2->getClientId());

            QCOMPARE(ses->username, ses2->username);
            QCOMPARE(ses->client_id, ses2->client_id);
            QCOMPARE(ses->incomingQoS2MessageIds, ses2->incomingQoS2MessageIds);
            QCOMPARE(ses->outgoingQoS2MessageIds, ses2->outgoingQoS2MessageIds);
            QCOMPARE(ses->nextPacketId, ses2->nextPacketId);
        }

        std::unordered_map<std::string, std::list<SubscriptionForSerializing>> store1Subscriptions;
        store->getSubscriptions(&store->root, "", true, store1Subscriptions);

        std::unordered_map<std::string, std::list<SubscriptionForSerializing>> store2Subscriptions;
        store2->getSubscriptions(&store->root, "", true, store2Subscriptions);

        MYCASTCOMPARE(store1Subscriptions.size(), 4);
        MYCASTCOMPARE(store2Subscriptions.size(), 4);

        int noLocalCount = 0;
        int retainAsPublishedCount = 0;

        for(auto &pair : store1Subscriptions)
        {
            std::list<SubscriptionForSerializing> &subscList1 = pair.second;
            std::list<SubscriptionForSerializing> &subscList2 = store2Subscriptions[pair.first];

            QCOMPARE(subscList1.size(), subscList2.size());

            auto subs1It = subscList1.begin();
            auto subs2It = subscList2.begin();

            while (subs1It != subscList1.end())
            {
                SubscriptionForSerializing &one = *subs1It;
                SubscriptionForSerializing &two = *subs2It;
                QCOMPARE(one.clientId, two.clientId);
                QCOMPARE(one.qos, two.qos);
                QCOMPARE(one.noLocal, two.noLocal);
                QCOMPARE(one.retainAsPublished, two.retainAsPublished);

                if (two.noLocal)
                    noLocalCount++;

                if (two.retainAsPublished)
                    retainAsPublishedCount++;

                subs1It++;
                subs2It++;
            }

        }

        QVERIFY(noLocalCount > 0);
        QVERIFY(retainAsPublishedCount == 1);

        std::shared_ptr<Session> loadedSes = store2->sessionsById["c1"];
        std::shared_ptr<QueuedPublish> queuedPublishLoaded = loadedSes->qosPacketQueue.popNext();

        QCOMPARE(queuedPublishLoaded->getPublish().topic, "a/b/c");
        QCOMPARE(queuedPublishLoaded->getPublish().payload, "Hello Barry");
        QCOMPARE(queuedPublishLoaded->getPublish().qos, 1);
        QCOMPARE(queuedPublishLoaded->getPublish().client_id, "ClientIdFromFakePublisher");
        QCOMPARE(queuedPublishLoaded->getPublish().username, "UsernameFromFakePublisher");
        QCOMPARE(queuedPublishLoaded->getPublish().expiresAfter.count(), 9);
        QCOMPARE(queuedPublishLoaded->getPublish().getAge().count(), 1);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}

void MainTests::testParsePacketHelper(const std::string &topic, uint8_t from_qos, bool retain)
{
    Logger::getInstance()->setFlags(LogLevel::None, false);

    Settings settings;
    settings.logLevel = LogLevel::Info;
    std::shared_ptr<SubscriptionStore> store(new SubscriptionStore());
    PluginLoader pluginLoader;
    std::shared_ptr<ThreadData> t(new ThreadData(0, settings, pluginLoader));

    // Kind of a hack...
    Authentication auth(settings);
    ThreadGlobals::assign(&auth);

    std::shared_ptr<Client> dummyClient(new Client(0, t, nullptr, false, false, nullptr, settings, false));
    dummyClient->setClientProperties(ProtocolVersion::Mqtt311, "qostestclient", "user1", true, 60);
    store->registerClientAndKickExistingOne(dummyClient, false, 512, 120);

    uint16_t packetid = 66;
    for (int len = 0; len < 150; len++ )
    {
        const uint16_t pack_id = packetid++;

        std::vector<MqttPacket> parsedPackets;

        const std::string payloadOne = getSecureRandomString(len);
        Publish pubOne(topic, payloadOne, from_qos);
        pubOne.retain = retain;
        MqttPacket stagingPacketOne(ProtocolVersion::Mqtt311, pubOne);
        if (from_qos > 0)
            stagingPacketOne.setPacketId(pack_id);
        CirBuf stagingBufOne(1024);
        stagingPacketOne.readIntoBuf(stagingBufOne);

        MqttPacket::bufferToMqttPackets(stagingBufOne, parsedPackets, dummyClient);
        QVERIFY(parsedPackets.size() == 1);
        MqttPacket parsedPacketOne = std::move(parsedPackets.front());
        parsedPacketOne.parsePublishData();
        if (retain) // A normal handled packet always has retain=0, so I force setting it here.
            parsedPacketOne.setRetain(true);

        QCOMPARE(stagingPacketOne.getTopic(), parsedPacketOne.getTopic());
        QCOMPARE(stagingPacketOne.getPayloadCopy(), parsedPacketOne.getPayloadCopy());
        QCOMPARE(stagingPacketOne.getRetain(), parsedPacketOne.getRetain());
        QCOMPARE(stagingPacketOne.getQos(), parsedPacketOne.getQos());
        QCOMPARE(stagingPacketOne.first_byte, parsedPacketOne.first_byte);
    }
}

/**
 * @brief MainTests::testCopyPacket tests the actual bytes of a published packet that would be written to a client.
 */
void MainTests::testParsePacket()
{
    for (int retain = 0; retain < 2; retain++)
    {
        testParsePacketHelper("John/McLane", 0, retain);
        testParsePacketHelper("Ben/Sisko", 1, retain);
        testParsePacketHelper("Rebecca/Bunch", 2, retain);

        testParsePacketHelper("Buffy/Slayer", 1, retain);
        testParsePacketHelper("Sarah/Connor", 2, retain);
        testParsePacketHelper("Susan/Mayer", 2, retain);
    }
}

void testDowngradeQoSOnSubscribeHelper(const uint8_t pub_qos, const uint8_t sub_qos)
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

            const std::string topic("Star/Trek");
            const std::string payload("Captain Kirk");

            sender.connectClient(senderVersion);
            receiver.connectClient(receiverVersion);

            receiver.subscribe(topic, sub_qos);
            sender.publish(topic, payload, pub_qos);

            receiver.waitForMessageCount(1);

            MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
            MqttPacket &recv = receiver.receivedPublishes.front();

            const uint8_t expected_qos = std::min<const uint8_t>(pub_qos, sub_qos);
            QVERIFY2(recv.getQos() == expected_qos, formatString("Failure: received QoS is %d. Published is %d. Subscribed as %d. Expected QoS is %d",
                                                                 recv.getQos(), pub_qos, sub_qos, expected_qos).c_str());
            QVERIFY(recv.getTopic() == topic);
            QVERIFY(recv.getPayloadCopy() == payload);
        }
    }
}

void MainTests::testDowngradeQoSOnSubscribeQos2to2()
{
    testDowngradeQoSOnSubscribeHelper(2, 2);
}

void MainTests::testDowngradeQoSOnSubscribeQos2to1()
{
    testDowngradeQoSOnSubscribeHelper(2, 1);
}

void MainTests::testDowngradeQoSOnSubscribeQos2to0()
{
    testDowngradeQoSOnSubscribeHelper(2, 0);
}

void MainTests::testDowngradeQoSOnSubscribeQos1to1()
{
    testDowngradeQoSOnSubscribeHelper(1, 1);
}

void MainTests::testDowngradeQoSOnSubscribeQos1to0()
{
    testDowngradeQoSOnSubscribeHelper(1, 0);
}

void MainTests::testDowngradeQoSOnSubscribeQos0to0()
{
    testDowngradeQoSOnSubscribeHelper(0, 0);
}

/**
 * @brief MainTests::testNotMessingUpQosLevels was divised because we optimize by preventing packet copies. This entails changing the vector of the original
 * incoming packet, resulting in possibly changing values like QoS levels for later subscribers.
 */
void MainTests::testNotMessingUpQosLevels()
{
    const std::string topic = "HK7c1MFu6kdT69fWY";
    const std::string payload = "M4XK2LZ2Smaazba8RobZOgoe6CENxCll";

    std::list<ProtocolVersion> senderVersions {ProtocolVersion::Mqtt31, ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5};
    std::list<ProtocolVersion> receiverVersions {ProtocolVersion::Mqtt31, ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5};

    for (ProtocolVersion senderVersion : senderVersions)
    {
        for (ProtocolVersion receiverVersion : receiverVersions)
        {

            FlashMQTestClient testContextSender;
            FlashMQTestClient testContextReceiver1;
            FlashMQTestClient testContextReceiver2;
            FlashMQTestClient testContextReceiver3;
            FlashMQTestClient testContextReceiver4;
            FlashMQTestClient testContextReceiver5;
            FlashMQTestClient testContextReceiverMqtt3;
            FlashMQTestClient testContextReceiverMqtt5;

            testContextReceiver1.start();
            testContextReceiver1.connectClient(receiverVersion);
            testContextReceiver1.subscribe(topic, 0);

            testContextReceiver2.start();
            testContextReceiver2.connectClient(receiverVersion);
            testContextReceiver2.subscribe(topic, 1);

            testContextReceiver3.start();
            testContextReceiver3.connectClient(receiverVersion);
            testContextReceiver3.subscribe(topic, 2);

            testContextReceiver4.start();
            testContextReceiver4.connectClient(receiverVersion);
            testContextReceiver4.subscribe(topic, 1);

            testContextReceiver5.start();
            testContextReceiver5.connectClient(receiverVersion);
            testContextReceiver5.subscribe(topic, 0);

            testContextReceiverMqtt3.start();
            testContextReceiverMqtt3.connectClient(ProtocolVersion::Mqtt311);
            testContextReceiverMqtt3.subscribe(topic, 0);

            testContextReceiverMqtt5.start();
            testContextReceiverMqtt5.connectClient(ProtocolVersion::Mqtt5);
            testContextReceiverMqtt5.subscribe(topic, 0);

            testContextSender.start();
            testContextSender.connectClient(senderVersion);
            testContextSender.publish(topic, payload, 2);

            testContextReceiver1.waitForMessageCount(1);
            testContextReceiver2.waitForMessageCount(1);
            testContextReceiver3.waitForMessageCount(1);
            testContextReceiver4.waitForMessageCount(1);
            testContextReceiver5.waitForMessageCount(1);

            MYCASTCOMPARE(testContextReceiver1.receivedPublishes.size(), 1);
            MYCASTCOMPARE(testContextReceiver2.receivedPublishes.size(), 1);
            MYCASTCOMPARE(testContextReceiver3.receivedPublishes.size(), 1);
            MYCASTCOMPARE(testContextReceiver4.receivedPublishes.size(), 1);
            MYCASTCOMPARE(testContextReceiver5.receivedPublishes.size(), 1);
            MYCASTCOMPARE(testContextReceiverMqtt3.receivedPublishes.size(), 1);
            MYCASTCOMPARE(testContextReceiverMqtt5.receivedPublishes.size(), 1);

            QCOMPARE(testContextReceiver1.receivedPublishes.front().getQos(), 0);
            QCOMPARE(testContextReceiver2.receivedPublishes.front().getQos(), 1);
            QCOMPARE(testContextReceiver3.receivedPublishes.front().getQos(), 2);
            QCOMPARE(testContextReceiver4.receivedPublishes.front().getQos(), 1);
            QCOMPARE(testContextReceiver5.receivedPublishes.front().getQos(), 0);
            QCOMPARE(testContextReceiverMqtt3.receivedPublishes.front().getQos(), 0);
            QCOMPARE(testContextReceiverMqtt5.receivedPublishes.front().getQos(), 0);

            QCOMPARE(testContextReceiver1.receivedPublishes.front().getPayloadCopy(), payload);
            QCOMPARE(testContextReceiver2.receivedPublishes.front().getPayloadCopy(), payload);
            QCOMPARE(testContextReceiver3.receivedPublishes.front().getPayloadCopy(), payload);
            QCOMPARE(testContextReceiver4.receivedPublishes.front().getPayloadCopy(), payload);
            QCOMPARE(testContextReceiver5.receivedPublishes.front().getPayloadCopy(), payload);
            QCOMPARE(testContextReceiverMqtt3.receivedPublishes.front().getPayloadCopy(), payload);
            QCOMPARE(testContextReceiverMqtt5.receivedPublishes.front().getPayloadCopy(), payload);

            QCOMPARE(testContextReceiver2.receivedPublishes.front().getPacketId(), 1);
            QCOMPARE(testContextReceiver3.receivedPublishes.front().getPacketId(), 1);
            QCOMPARE(testContextReceiver4.receivedPublishes.front().getPacketId(), 1);
        }
    }
}

void MainTests::testUnSubscribe()
{
    FlashMQTestClient sender;
    FlashMQTestClient receiver;

    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt311);

    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311);

    receiver.subscribe("Rebecca/Bunch", 2);
    receiver.subscribe("Josh/Chan", 1);
    receiver.subscribe("White/Josh", 1);

    sender.publish("Rebecca/Bunch", "Bunch here", 2);
    sender.publish("White/Josh", "Anteater", 2);
    sender.publish("Josh/Chan", "Human flip-flop", 2);

    receiver.waitForMessageCount(3);

    QVERIFY(std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](const MqttPacket &pack) {
        return pack.getPayloadCopy() == "Bunch here" && pack.getTopic() == "Rebecca/Bunch";
    }));

    QVERIFY(std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](const MqttPacket &pack) {
        return pack.getPayloadCopy() == "Anteater" && pack.getTopic() == "White/Josh";
    }));

    QVERIFY(std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](const MqttPacket &pack) {
        return pack.getPayloadCopy() == "Human flip-flop" && pack.getTopic() == "Josh/Chan";
    }));

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 3);

    receiver.clearReceivedLists();

    receiver.unsubscribe("Josh/Chan");

    sender.publish("Rebecca/Bunch", "Bunch here", 2);
    sender.publish("White/Josh", "Anteater", 2);
    sender.publish("Josh/Chan", "Human flip-flop", 2);

    receiver.waitForMessageCount(2);

    MYCASTCOMPARE(receiver.receivedPublishes.size(), 2);

    QVERIFY(std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](const MqttPacket &pack) {
        return pack.getPayloadCopy() == "Bunch here" && pack.getTopic() == "Rebecca/Bunch";
    }));

    QVERIFY(std::any_of(receiver.receivedPublishes.begin(), receiver.receivedPublishes.end(), [](const MqttPacket &pack) {
        return pack.getPayloadCopy() == "Anteater" && pack.getTopic() == "White/Josh";
    }));
}

/**
 * @brief MainTests::testBasicsWithFlashMQTestClient was used to develop FlashMQTestClient.
 */
void MainTests::testBasicsWithFlashMQTestClient()
{
    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt311);

    MqttPacket &connAckPack = client.receivedPackets.front();
    QVERIFY(connAckPack.packetType == PacketType::CONNACK);

    {
        client.subscribe("a/b", 1);

        MqttPacket &subAck = client.receivedPackets.front();
        SubAckData subAckData = subAck.parseSubAckData();

        QVERIFY(subAckData.subAckCodes.size() == 1);
        QVERIFY(subAckData.subAckCodes.front() == 1);
    }

    {
        client.subscribe("c/d", 2);

        MqttPacket &subAck = client.receivedPackets.front();
        SubAckData subAckData = subAck.parseSubAckData();

        QVERIFY(subAckData.subAckCodes.size() == 1);
        QVERIFY(subAckData.subAckCodes.front() == 2);
    }

    client.clearReceivedLists();

    FlashMQTestClient publisher;
    publisher.start();
    publisher.connectClient(ProtocolVersion::Mqtt5);

    {
        publisher.publish("a/b", "wave", 2);

        client.waitForMessageCount(1);
        MqttPacket &p = client.receivedPublishes.front();

        QCOMPARE(p.getPublishData().topic, "a/b");
        QCOMPARE(p.getPayloadCopy(), "wave");
        QCOMPARE(p.getPublishData().qos, 1);
        QVERIFY(p.getPacketId() > 0);
        QVERIFY(p.protocolVersion == ProtocolVersion::Mqtt311);
    }

    client.clearReceivedLists();

    {
        publisher.publish("c/d", "asdfasdfasdf", 2);

        client.waitForMessageCount(1);
        MqttPacket &p = client.receivedPublishes.back();

        MYCASTCOMPARE(client.receivedPublishes.size(), 1);

        QCOMPARE(p.getPublishData().topic, "c/d");
        QCOMPARE(p.getPayloadCopy(), "asdfasdfasdf");
        QCOMPARE(p.getPublishData().qos, 2);
        QVERIFY(p.getPacketId() > 1); // It's the same client, so it should not re-use packet id 1
        QVERIFY(p.protocolVersion == ProtocolVersion::Mqtt311);
    }


}

void MainTests::testDontRemoveSessionGivenToNewClientWithSameId()
{
    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311, true, 60, [] (Connect &c) {
        c.clientid = "Sandra-nonrandom";
    });
    receiver.subscribe("just/a/path", 0);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    {
        Publish pub("just/a/path", "AAAAA", 0);
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeTopicAlias(1);
        sender.publish(pub);
    }

    {
        receiver.waitForMessageCount(1);

        const MqttPacket &pack1 = receiver.receivedPublishes.at(0);

        QCOMPARE(pack1.getTopic(), "just/a/path");
        QCOMPARE(pack1.getPayloadCopy(), "AAAAA");
    }

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt311, true, 60, [] (Connect &c) {
        c.clientid = "Sandra-nonrandom";
    });
    receiver2.subscribe("just/a/path", 0);

    {
        Publish pub("just/a/path", "AAAAA", 0);
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeTopicAlias(1);
        sender.publish(pub);
    }

    {
        try
        {
            receiver2.waitForMessageCount(1);
        }
        catch(std::exception &ex)
        {
            QFAIL("The second subscriber did not get the message, so the subscription failed.");
        }

        const MqttPacket &pack1 = receiver2.receivedPublishes.at(0);

        QCOMPARE(pack1.getTopic(), "just/a/path");
        QCOMPARE(pack1.getPayloadCopy(), "AAAAA");
    }

}

void MainTests::testKeepSubscriptionOnKickingOutExistingClientWithCleanSessionFalse()
{
    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311, false, 60, [] (Connect &c) {
        c.clientid = "Carl-nonrandom";
    });
    receiver.subscribe("just/a/path", 0);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt311, false, 60, [] (Connect &c) {
        c.clientid = "Carl-nonrandom";
    });

    {
        Publish pub("just/a/path", "AAAAA", 0);
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeTopicAlias(1);
        sender.publish(pub);
    }

    {
        try
        {
            receiver2.waitForMessageCount(1);
        }
        catch(std::exception &ex)
        {
            QFAIL("The second subscriber did not get the message, so the subscription failed.");
        }

        const MqttPacket &pack1 = receiver2.receivedPublishes.at(0);

        QCOMPARE(pack1.getTopic(), "just/a/path");
        QCOMPARE(pack1.getPayloadCopy(), "AAAAA");
    }
}

void MainTests::testPickUpSessionWithSubscriptionsAfterDisconnect()
{
    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt311, false, 60, [] (Connect &c) {
        c.clientid = "Hungus-nonrandom";
    });
    receiver.subscribe("just/a/path", 0);
    receiver.disconnect(ReasonCodes::Success);

    usleep(50000);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt311, false, 60, [] (Connect &c) {
        c.clientid = "Hungus-nonrandom";
    });

    {
        Publish pub("just/a/path", "AAAAAB", 0);
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeTopicAlias(1);
        sender.publish(pub);
    }

    {
        try
        {
            receiver2.waitForMessageCount(1);
        }
        catch(std::exception &ex)
        {
            QFAIL("The second subscriber did not get the message, so the subscription failed.");
        }

        const MqttPacket &pack1 = receiver2.receivedPublishes.at(0);

        QCOMPARE(pack1.getTopic(), "just/a/path");
        QCOMPARE(pack1.getPayloadCopy(), "AAAAAB");
    }
}

void MainTests::testIncomingTopicAlias()
{
    FlashMQTestClient receiver;
    receiver.start();
    receiver.connectClient(ProtocolVersion::Mqtt5);

    receiver.subscribe("just/a/path", 0);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    {
        Publish pub("just/a/path", "AAAAA", 0);
        pub.constructPropertyBuilder();
        pub.propertyBuilder->writeTopicAlias(1);
        sender.publish(pub);
    }

    {
        Publish pub2("", "BBBBB", 0);
        pub2.constructPropertyBuilder();
        pub2.propertyBuilder->writeTopicAlias(1);
        sender.publish(pub2);
    }

    receiver.waitForMessageCount(2);

    const MqttPacket &pack1 = receiver.receivedPublishes.at(0);
    const MqttPacket &pack2 = receiver.receivedPublishes.at(1);

    QCOMPARE(pack1.getTopic(), "just/a/path");
    QCOMPARE(pack1.getPayloadCopy(), "AAAAA");

    QCOMPARE(pack2.getTopic(), "just/a/path");
    QCOMPARE(pack2.getPayloadCopy(), "BBBBB");
}

void MainTests::testOutgoingTopicAlias()
{
    FlashMQTestClient receiver1;
    receiver1.start();
    receiver1.connectClient(ProtocolVersion::Mqtt5, true, 300, [](Connect &connect){
        connect.propertyBuilder->writeMaxTopicAliases(10);
    });
    receiver1.subscribe("don't/be/a/laywer", 0);

    FlashMQTestClient receiver2;
    receiver2.start();
    receiver2.connectClient(ProtocolVersion::Mqtt5);
    receiver2.subscribe("don't/be/a/laywer", 0);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt311);

    sender.publish("don't/be/a/laywer", "ABCDEF", 0);
    sender.publish("don't/be/a/laywer", "ABCDEF", 0);

    receiver1.waitForMessageCount(2);
    receiver2.waitForMessageCount(2);

    {
        const MqttPacket &fullPacket = receiver1.receivedPublishes.at(0);
        QCOMPARE(fullPacket.getTopic(), "don't/be/a/laywer");
        QCOMPARE(fullPacket.getPayloadCopy(), "ABCDEF");
        MYCASTCOMPARE(fullPacket.bites.size(), 31);
        std::string arrayContent(fullPacket.bites.data(), fullPacket.bites.size());
        QVERIFY(strContains(arrayContent, "don't/be/a/laywer"));
    }

    {
        const MqttPacket &shorterPacket = receiver1.receivedPublishes.at(1);
        QCOMPARE(shorterPacket.getTopic(), "don't/be/a/laywer");
        QCOMPARE(shorterPacket.getPayloadCopy(), "ABCDEF");
        MYCASTCOMPARE(shorterPacket.bites.size(), 14);
        std::string arrayContent(shorterPacket.bites.data(), shorterPacket.bites.size());
        QVERIFY(!strContains(arrayContent, "don't/be/a/laywer"));
    }

    MYCASTCOMPARE(receiver2.receivedPublishes.size(), 2);

    std::for_each(receiver2.receivedPublishes.begin(), receiver2.receivedPublishes.end(), [](MqttPacket &packet) {
        QCOMPARE(packet.getTopic(), "don't/be/a/laywer");
        QCOMPARE(packet.getPayloadCopy(), "ABCDEF");
        MYCASTCOMPARE(packet.bites.size(), 28); // That's 3 less than the other one, because the alias id is not there.
    });
}

void MainTests::testOutgoingTopicAliasStoredPublishes()
{
    std::unique_ptr<FlashMQTestClient> sender = std::make_unique<FlashMQTestClient>();
    sender->start();
    std::shared_ptr<WillPublish> will = std::make_shared<WillPublish>();
    will->topic = "last/dance/long/long";
    will->payload = "will payload";
    sender->setWill(will);
    sender->connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver1;
    receiver1.start();
    receiver1.connectClient(ProtocolVersion::Mqtt5, true, 300, [](Connect &connect){
        connect.propertyBuilder->writeMaxTopicAliases(10);
    });
    receiver1.subscribe("last/dance/#", 0);

    FlashMQTestClient sender2;
    sender2.start();
    sender2.connectClient(ProtocolVersion::Mqtt5);

    // Establish the first time use of the topic for the alias.
    sender2.publish("last/dance/long/long", "normal payload", 0);

    receiver1.waitForMessageCount(1);
    QCOMPARE(receiver1.receivedPublishes.front().getPayloadCopy(), "normal payload");
    MYCASTCOMPARE(receiver1.receivedPublishes.front().bites.size(), 42);

    receiver1.clearReceivedLists();

    // This will send a will, which should re-use the alias.
    sender.reset();

    receiver1.waitForMessageCount(1);
    QCOMPARE(receiver1.receivedPublishes.front().getPayloadCopy(), "will payload");
    QVERIFY(receiver1.receivedPublishes.front().bites.size() < 35);
}

void MainTests::testReceivingRetainedMessageWithQoS()
{
    int testCount = 0;

    for (uint8_t sendQos = 0; sendQos < 3; sendQos++)
    {
        for (uint8_t subscribeQos = 0; subscribeQos < 3; subscribeQos++)
        {
            testCount++;

            FlashMQTestClient sender;
            sender.start();

            const std::string payload = "We are testing";

            sender.connectClient(ProtocolVersion::Mqtt311);

            Publish p1("topic1/FOOBAR", payload, sendQos);
            p1.retain = true;
            sender.publish(p1);

            FlashMQTestClient receiver;
            receiver.start();
            receiver.connectClient(ProtocolVersion::Mqtt5);

            receiver.subscribe("+/+", subscribeQos);

            receiver.waitForMessageCount(1);

            const uint8_t expQos = std::min<uint8_t>(sendQos, subscribeQos);

            MYCASTCOMPARE(receiver.receivedPublishes.size(), 1);
            MYCASTCOMPARE(receiver.receivedPublishes.front().getQos(), expQos);
            MYCASTCOMPARE(receiver.receivedPublishes.front().getTopic(), "topic1/FOOBAR");
            MYCASTCOMPARE(receiver.receivedPublishes.front().getPayloadCopy(), payload);
            MYCASTCOMPARE(receiver.receivedPublishes.front().getRetain(), true);
        }
    }

    MYCASTCOMPARE(9, testCount);
}

void MainTests::testQosDowngradeOnOfflineClients()
{
    int testCount = 0;

    std::vector<std::string> subscribePaths {"topic1/FOOBAR", "+/+", "#"};

    for (uint8_t sendQos = 1; sendQos < 3; sendQos++)
    {
        for (uint8_t subscribeQos = 1; subscribeQos < 3; subscribeQos++)
        {
            for (const std::string &subscribePath : subscribePaths)
            {
                testCount++;

                // First start with clean_start to reset the session.
                std::unique_ptr<FlashMQTestClient> receiver = std::make_unique<FlashMQTestClient>();
                receiver->start();
                receiver->connectClient(ProtocolVersion::Mqtt5, true, 600, [](Connect &connect) {
                    connect.clientid = "TheReceiver";
                });
                receiver->subscribe(subscribePath, subscribeQos);
                receiver->disconnect(ReasonCodes::Success);
                receiver.reset();

                const std::string payload = "We are testing";

                FlashMQTestClient sender;
                sender.start();
                sender.connectClient(ProtocolVersion::Mqtt311);

                Publish p1("topic1/FOOBAR", payload, sendQos);

                for (int i = 0; i < 10; i++)
                {
                    sender.publish(p1);
                }

                // Now we connect again, and we should now pick up the existing session.
                receiver = std::make_unique<FlashMQTestClient>();
                receiver->start();
                receiver->connectClient(ProtocolVersion::Mqtt5, false, 600, [](Connect &connect) {
                    connect.clientid = "TheReceiver";
                });

                receiver->waitForMessageCount(10);

                const uint8_t expQos = std::min<uint8_t>(sendQos, subscribeQos);

                MYCASTCOMPARE(receiver->receivedPublishes.size(), 10);

                QVERIFY(std::all_of(receiver->receivedPublishes.begin(), receiver->receivedPublishes.end(), [&](MqttPacket &pack) { return pack.getQos() == expQos;}));
                QVERIFY(std::all_of(receiver->receivedPublishes.begin(), receiver->receivedPublishes.end(), [&](MqttPacket &pack) { return pack.getTopic() == "topic1/FOOBAR";}));
                QVERIFY(std::all_of(receiver->receivedPublishes.begin(), receiver->receivedPublishes.end(), [&](MqttPacket &pack) { return pack.getPayloadCopy() == payload;}));

                receiver.reset();

                // Now we connect again, and we should get nothing
                receiver = std::make_unique<FlashMQTestClient>();
                receiver->start();
                receiver->connectClient(ProtocolVersion::Mqtt5, false, 600, [](Connect &connect) {
                    connect.clientid = "TheReceiver";
                });

                usleep(100000);

                QVERIFY(receiver->receivedPublishes.empty());
            }
        }
    }

    MYCASTCOMPARE(12, testCount);
}

void MainTests::testUserProperties()
{
    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5);

    FlashMQTestClient receiver5;
    receiver5.start();
    receiver5.connectClient(ProtocolVersion::Mqtt5);
    receiver5.subscribe("#", 1);

    FlashMQTestClient receiver3;
    receiver3.start();
    receiver3.connectClient(ProtocolVersion::Mqtt311);
    receiver3.subscribe("#", 1);

    Publish pub("I'm/going/to/leave/a/message", "boo", 1);
    pub.constructPropertyBuilder();
    pub.propertyBuilder->writeUserProperty("mykey", "myval");
    pub.propertyBuilder->writeUserProperty("mykeyhaha", "myvalhaha");
    sender.publish(pub);

    receiver5.waitForMessageCount(1);
    receiver3.waitForMessageCount(1);

    MqttPacket &pack5 = receiver5.receivedPublishes.front();

    const std::vector<std::pair<std::string, std::string>> *properties5 = pack5.getUserProperties();

    QVERIFY(properties5);
    MYCASTCOMPARE(properties5->size(), 2);

    const std::pair<std::string, std::string> &firstPair = properties5->operator[](0);
    const std::pair<std::string, std::string> &secondPair = properties5->operator[](1);

    QCOMPARE(firstPair.first, "mykey");
    QCOMPARE(firstPair.second, "myval");

    QCOMPARE(secondPair.first, "mykeyhaha");
    QCOMPARE(secondPair.second, "myvalhaha");

    MqttPacket &pack3 = receiver3.receivedPublishes.front();
    const std::vector<std::pair<std::string, std::string>> *properties3 = pack3.getUserProperties();
    QVERIFY(properties3 == nullptr);
}

void MainTests::testMessageExpiry()
{
    std::unique_ptr<FlashMQTestClient> receiver;

    auto makeReceiver = [&](){
        receiver = std::make_unique<FlashMQTestClient>();
        receiver->start();
        receiver->connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &c) {
            c.clientid = "pietje";
        });
    };

    makeReceiver();
    receiver->subscribe("#", 2);

    FlashMQTestClient sender;
    sender.start();
    sender.connectClient(ProtocolVersion::Mqtt5, true, 120);

    Publish publishMe("a/b/c/d/e", "smoke", 1);
    publishMe.constructPropertyBuilder();
    publishMe.setExpireAfter(1);
    sender.publish(publishMe);

    // First we test that a message with an expiry set is still immediately delivered to active clients.

    receiver->waitForMessageCount(1);

    QVERIFY(receiver->receivedPublishes.size() == 1);
    QCOMPARE(receiver->receivedPublishes.front().getTopic(), "a/b/c/d/e");
    QCOMPARE(receiver->receivedPublishes.front().getPayloadCopy(), "smoke");

    // Then we test delivering it to an offline client and see if we get it if we are fast enough.

    receiver.reset();
    publishMe.setExpireAfter(1);
    sender.publish(publishMe);
    usleep(300000);
    makeReceiver();
    receiver->waitForMessageCount(1);

    MYCASTCOMPARE(receiver->receivedPublishes.size(), 1);
    QCOMPARE(receiver->receivedPublishes.front().getTopic(), "a/b/c/d/e");
    QCOMPARE(receiver->receivedPublishes.front().getPayloadCopy(), "smoke");

    // Then we test delivering it to an offline client that comes back too late.

    publishMe.setExpireAfter(1);
    receiver.reset();
    sender.publish(publishMe);
    usleep(2100000);
    makeReceiver();
    usleep(100000);
    receiver->waitForMessageCount(0);
    QVERIFY(receiver->receivedPublishes.empty());

}

/**
 * @brief MainTests::testExpiredQueuedMessages Tests whether expiring messages clear out and make room in the send quota / receive maximum.
 */
void MainTests::testExpiredQueuedMessages()
{
    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous yes");
    confFile.writeLine("max_qos_msg_pending_per_client 32");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    for (ProtocolVersion version : versions)
    {
        FlashMQTestClient sender;
        sender.start();
        sender.connectClient(ProtocolVersion::Mqtt5, true, 120);

        std::unique_ptr<FlashMQTestClient> receiver;

        auto makeReceiver = [&]() {
            receiver = std::make_unique<FlashMQTestClient>();
            receiver->start();
            receiver->connectClient(version, false, 120, [](Connect &c) {
                c.clientid = "ReceiverDude01";
            });
        };

        makeReceiver();
        receiver->subscribe("#", 2);
        receiver.reset();

        Publish expiringPub("this/one/expires", "Calimex", 1);
        expiringPub.setExpireAfter(1);

        sender.publish(expiringPub);

        for (int i = 0; i < 35; i++)
        {
            Publish normalPub(formatString("topic/%d", i), "adsf", 1);
            sender.publish(normalPub);
        }

        makeReceiver();

        try
        {
            receiver->waitForMessageCount(32);
        }
        catch (std::exception &ex)
        {
            MYCASTCOMPARE(receiver->receivedPublishes.size(), 32);
        }

        MYCASTCOMPARE(receiver->receivedPublishes.size(), 32);
        QVERIFY(std::any_of(receiver->receivedPublishes.begin(), receiver->receivedPublishes.end(), [](MqttPacket &p) {
            return p.getTopic() == "this/one/expires";
        }));

        receiver.reset();

        for (int i = 0; i < 20; i++)
        {
            Publish normalPub(formatString("late/topic/%d", i), "adsf", 1);
            sender.publish(normalPub);
        }

        sender.publish(expiringPub);
        usleep(2000000);

        // Now that we've waited, there should be an extra spot.

        for (int i = 0; i < 15; i++)
        {
            Publish normalPub(formatString("topic/%d", i), "adsf", 1);
            sender.publish(normalPub);
        }

        makeReceiver();

        try
        {
            receiver->waitForMessageCount(32);
        }
        catch (std::exception &ex)
        {
            MYCASTCOMPARE(receiver->receivedPublishes.size(), 32);
        }

        MYCASTCOMPARE(receiver->receivedPublishes.size(), 32);
        QVERIFY(std::none_of(receiver->receivedPublishes.begin(), receiver->receivedPublishes.end(), [](MqttPacket &p) {
            return p.getTopic() == "this/one/expires";
        }));

        int pi = 0;

        for (int i = 0; i < 20; i++)
        {
            QCOMPARE(receiver->receivedPublishes[pi++].getTopic(), formatString("late/topic/%d", i));
        }

        for (int i = 0; i < 12; i++)
        {
            QCOMPARE(receiver->receivedPublishes[pi++].getTopic(), formatString("topic/%d", i));
        }

        MYCASTCOMPARE(pi, 32);
    }
}

/**
 * @brief MainTests::testQoSPublishQueue tests the queue and it's insertion order linked list.
 */
void MainTests::testQoSPublishQueue()
{
    QoSPublishQueue q;
    uint16_t id = 1;

    std::shared_ptr<QueuedPublish> qp;

    {
        Publish p1("one", "onepayload", 1);
        q.queuePublish(std::move(p1), id++);

        qp = q.popNext();
        QVERIFY(qp);
        QCOMPARE(qp->getPublish().topic, "one");
        QCOMPARE(qp->getPublish().payload, "onepayload");
        qp = q.popNext();
        QVERIFY(!qp);
    }

    {
        Publish p1("two", "asdf", 1);
        Publish p2("three", "wer", 1);
        q.queuePublish(std::move(p1), id++);
        q.queuePublish(std::move(p2), id++);

        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "two");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "three");
        qp = q.popNext();
        QVERIFY(!qp);
    }

    {
        Publish p1("four", "asdf", 1);
        Publish p2("five", "wer", 1);
        Publish p3("six", "wer", 1);
        q.queuePublish(std::move(p1), id++);
        q.queuePublish(std::move(p2), id++);
        q.queuePublish(std::move(p3), id++);

        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "four");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "five");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "six");
        qp = q.popNext();
        QVERIFY(!qp);
    }

    // Remove middle
    {
        uint16_t idToRemove = 0;

        Publish p1("seven", "asdf", 1);
        Publish p2("eight", "wer", 1);
        Publish p3("nine", "wer", 1);
        q.queuePublish(std::move(p1), id++);
        idToRemove = id;
        q.queuePublish(std::move(p2), id++);
        q.queuePublish(std::move(p3), id++);

        q.erase(idToRemove);

        Publish p4("tool2eW7", "wer", 1);
        q.queuePublish(std::move(p4), id++);

        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "seven");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "nine");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "tool2eW7");
        qp = q.popNext();
        QVERIFY(!qp);
    }

    // Remove first
    {
        uint16_t idToRemove = 0;

        Publish p1("ten", "asdf", 1);
        Publish p2("eleven", "wer", 1);
        Publish p3("twelve", "wer", 1);
        idToRemove = id;
        q.queuePublish(std::move(p1), id++);
        q.queuePublish(std::move(p2), id++);
        q.queuePublish(std::move(p3), id++);

        q.erase(idToRemove);

        Publish p4("iew2Bie1", "wer", 1);
        q.queuePublish(std::move(p4), id++);

        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "eleven");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "twelve");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "iew2Bie1");
        qp = q.popNext();
        QVERIFY(!qp);
    }

    // Remove last
    {
        uint16_t idToRemove = 0;

        Publish p1("13", "asdf", 1);
        Publish p2("14", "wer", 1);
        Publish p3("15", "wer", 1);
        q.queuePublish(std::move(p1), id++);
        q.queuePublish(std::move(p2), id++);
        idToRemove = id;
        q.queuePublish(std::move(p3), id++);

        q.erase(idToRemove);

        Publish p4("16", "wer", 1);
        q.queuePublish(std::move(p4), id++);

        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "13");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "14");
        qp = q.popNext();
        QCOMPARE(qp->getPublish().topic, "16");
        qp = q.popNext();
        QVERIFY(!qp);
    }
}

void MainTests::testTimePointToAge()
{
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    auto past = now - std::chrono::seconds(11);

    auto p = ageFromTimePoint(past);
    auto pastCalculated = timepointFromAge(p);

    std::chrono::seconds diff = std::chrono::duration_cast<std::chrono::seconds>(pastCalculated - past);
    MYCASTCOMPARE(diff.count(), 0);
}

void MainTests::testMosquittoPasswordFile()
{
    std::vector<ProtocolVersion> versions { ProtocolVersion::Mqtt311, ProtocolVersion::Mqtt5 };

    ConfFileTemp passwd_file;
    passwd_file.writeLine("one:$6$JCNyGIZwxpaB++iTCwiT2e80YX6mEFymRCkRpHkm50dNP8IfHMWz97BdadZVsZCCC9yr7/OXxAbdfAVk71xqyA==$AL25hdhMm0CkQ3/nxtgGJ96xfSv6hCAf7aHZby8mZWnkNxmvRnuu6fHWi6yvyr1EjPD4P9vmIvKwqvdKEVDLLQ==");
    passwd_file.writeLine("two:$7$101$QVgLoPCu8Lb9A6HRYFhcsIsYqE1QR5elwDr7oioyNw7n5OMqdpM0Xk+Iacbj+ZvXiIVihFYEVDgJMkr8vAR08A==$xTJ1tbPTZcaJH+ie9gXUDumHqdJYpGCMXW/asC/qMrdobawqU2tpBHzvJnm2VfsYCwgchOCegI8RvYt1IAUivg==");
    passwd_file.closeFile();

    ConfFileTemp confFile;
    confFile.writeLine(formatString("mosquitto_password_file %s", passwd_file.getFilePath().c_str()));
    confFile.writeLine("allow_anonymous false");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
            connect.username = "one";
            connect.password = "one";
        });

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
            connect.username = "two";
            connect.password = "two";
        });

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
            connect.username = "two";
            connect.password = "wrongpasswordforexistinguser";
        });

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
            connect.username = "erqwerq";
            connect.password = "nonexistinguser";
        });

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }
}

void MainTests::testOverrideAllowAnonymousToTrue()
{
    ConfFileTemp passwd_file;
    passwd_file.writeLine("one:$6$JCNyGIZwxpaB++iTCwiT2e80YX6mEFymRCkRpHkm50dNP8IfHMWz97BdadZVsZCCC9yr"
                          "7/OXxAbdfAVk71xqyA==$AL25hdhMm0CkQ3/nxtgGJ96xfSv6hCAf7aHZby8mZWnkNxmvRnuu6f"
                          "HWi6yvyr1EjPD4P9vmIvKwqvdKEVDLLQ==");
    passwd_file.closeFile();

    ConfFileTemp confFile;
    confFile.writeLine(formatString("mosquitto_password_file %s", passwd_file.getFilePath().c_str()));
    confFile.writeLine("allow_anonymous false");
    confFile.writeLine(R"(
listen {
        protocol mqtt
        port 2883
        allow_anonymous true
}
listen {
        protocol mqtt
        port 2884
})");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    // With users, both listeners should act the same.
    for (int port : {2883, 2884})
    {
        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                connect.username = "one";
                connect.password = "one";
            }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
        }

        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                    connect.username = "one";
                    connect.password = "wrong";
                }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
        }
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2883);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2884);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                connect.username = "doesntexist";
                connect.password = "asdf";
            }, 2883); // allow_anonymous true override

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                connect.username = "doesntexist";
                connect.password = "asdf";
            }, 2884); // allow_anonymous false global setting.

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }

    // Test empty password for existing user. It should not think it's anonymous.
    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                connect.username = "one";
                connect.password = "";
            }, 2883); // allow_anonymous true global setting.

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }
}

void MainTests::testOverrideAllowAnonymousToFalse()
{
    ConfFileTemp passwd_file;
    passwd_file.writeLine("one:$6$JCNyGIZwxpaB++iTCwiT2e80YX6mEFymRCkRpHkm50dNP8IfHMWz97BdadZVsZCCC9yr"
                          "7/OXxAbdfAVk71xqyA==$AL25hdhMm0CkQ3/nxtgGJ96xfSv6hCAf7aHZby8mZWnkNxmvRnuu6f"
                          "HWi6yvyr1EjPD4P9vmIvKwqvdKEVDLLQ==");
    passwd_file.closeFile();

    ConfFileTemp confFile;
    confFile.writeLine(formatString("mosquitto_password_file %s", passwd_file.getFilePath().c_str()));
    confFile.writeLine("allow_anonymous true");
    confFile.writeLine("zero_byte_username_is_anonymous true");
    confFile.writeLine(R"(
listen {
        protocol mqtt
        port 2883
        allow_anonymous false
}
listen {
        protocol mqtt
        port 2884
})");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    // With users, both listeners should act the same.
    for (int port : {2883, 2884})
    {
        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                    connect.username = "one";
                    connect.password = "one";
                }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
        }

        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                    connect.username = "one";
                    connect.password = "wrong";
                }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
        }
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2883);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2884);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    // Test 'zero_byte_username_is_anonymous true'
    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt311, false, 120, [](Connect &connect) {
                connect.username = "";
                connect.password = "wrong";
            }, 2884);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                connect.username = "doesntexist";
                connect.password = "asdf";
            }, 2883); // allow_anonymous false override

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                connect.username = "doesntexist";
                connect.password = "asdf";
            }, 2884); // allow_anonymous true global setting.

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }
}

void MainTests::testKeepAllowAnonymousFalse()
{
    ConfFileTemp passwd_file;
    passwd_file.writeLine("one:$6$JCNyGIZwxpaB++iTCwiT2e80YX6mEFymRCkRpHkm50dNP8IfHMWz97BdadZVsZCCC9yr"
                          "7/OXxAbdfAVk71xqyA==$AL25hdhMm0CkQ3/nxtgGJ96xfSv6hCAf7aHZby8mZWnkNxmvRnuu6f"
                          "HWi6yvyr1EjPD4P9vmIvKwqvdKEVDLLQ==");
    passwd_file.closeFile();

    ConfFileTemp confFile;
    confFile.writeLine(formatString("mosquitto_password_file %s", passwd_file.getFilePath().c_str()));
    confFile.writeLine("allow_anonymous false");
    confFile.writeLine(R"(
listen {
        protocol mqtt
        port 2883
        allow_anonymous false
}
listen {
        protocol mqtt
        port 2884
})");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    // With users, both listeners should act the same.
    for (int port : {2883, 2884})
    {
        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                    connect.username = "one";
                    connect.password = "one";
                }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
        }

        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                    connect.username = "one";
                    connect.password = "wrong";
                }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
        }
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2883);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2884);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }
}

void MainTests::testAllowAnonymousWithoutPasswordsLoaded()
{
    ConfFileTemp confFile;
    confFile.writeLine("allow_anonymous true");
    confFile.writeLine(R"(
listen {
        protocol mqtt
        port 2883
        allow_anonymous true
}
listen {
        protocol mqtt
        port 2884
}
listen {
        protocol mqtt
        port 2885
        allow_anonymous false
})");
    confFile.closeFile();

    std::vector<std::string> args {"--config-file", confFile.getFilePath()};

    cleanup();
    init(args);

    // With users, both listeners should act the same.
    for (int port : {2883, 2884})
    {
        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                    connect.username = "one";
                    connect.password = "one";
                }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
        }

        {
            FlashMQTestClient client;
            client.start();
            client.connectClient(ProtocolVersion::Mqtt5, false, 120, [](Connect &connect) {
                    connect.username = "one";
                    connect.password = "wrong";
                }, port);

            auto ack = client.receivedPackets.front();
            ConnAckData ackData = ack.parseConnAckData();
            QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
        }
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2883);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2884);

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::Success);
    }

    {
        FlashMQTestClient client;
        client.start();
        client.connectClient(ProtocolVersion::Mqtt5, 2885); // allow_anonymous false

        auto ack = client.receivedPackets.front();
        ConnAckData ackData = ack.parseConnAckData();
        QCOMPARE(ackData.reasonCode, ReasonCodes::NotAuthorized);
    }
}


void MainTests::testAddrMatchesSubnetIpv4()
{
    struct sockaddr_in reference_addr;
    memset(&reference_addr, 0, sizeof(struct sockaddr_in));
    reference_addr.sin_family = AF_INET;
    QVERIFY(inet_pton(AF_INET, "12.13.14.15", &reference_addr.sin_addr) == 1);

    const std::list<std::string> positives = {"12.13.14.15", "12.13.14.15/24", "12.13.14.00/24", "0.0.0.0/0"};
    const std::list<std::string> negatives = {"12.13.99.00/24", "12.13.14.16/32", "11.13.14.15/32", "12.13.14.16"};

    for(const std::string &network : positives)
    {
        Network net(network);
        QVERIFY2(net.match(&reference_addr), formatString("'%s' failed", network.c_str()).c_str());
    }

    for(const std::string &network : negatives)
    {
        Network net(network);
        QVERIFY2(!net.match(&reference_addr), formatString("'%s' failed", network.c_str()).c_str());
    }

}

void MainTests::testAddrMatchesSubnetIpv6()
{
    struct sockaddr_in6 reference_addr;
    memset(&reference_addr, 0, sizeof(struct sockaddr_in6));
    reference_addr.sin6_family = AF_INET6;
    QVERIFY(inet_pton(AF_INET6, "2001:db8::1", &reference_addr.sin6_addr) == 1);

    const std::list<std::string> positives = {"2001:db8::1", "2001:db8:0::1", "2001:db8::1337:2/64", "2001:db8:2001:db8:1337::2/32", "3001:db8:2001:db8:1337::2/0"};
    const std::list<std::string> negatives = {"2002:db8::1", "2001:db8::2", "2003:db8::1/64"};

    for(const std::string &network : positives)
    {
        Network net(network);
        QVERIFY2(net.match(&reference_addr), formatString("Equality '%s' failed", network.c_str()).c_str());
    }

    for(const std::string &network : negatives)
    {
        Network net(network);
        QVERIFY2(!net.match(&reference_addr), formatString("Inequality '%s' failed", network.c_str()).c_str());
    }

    for (int i = 0; i < 128; i++)
    {
        std::string addressWithMask = formatString("2001:db8:0::1/%d", i);
        Network net(addressWithMask);
        QVERIFY2(net.match(&reference_addr), formatString("Fail mask range equal check: %s", addressWithMask.c_str()).c_str());
    }

    for (int i = 1; i < 128; i++)
    {
        std::string addressWithMask = formatString("f001:db8:0::1/%d", i);
        Network net(addressWithMask);
        Network netCopy(net);
        QVERIFY2(!netCopy.match(&reference_addr), formatString("Fail mask range not equal check: %s", addressWithMask.c_str()).c_str());
    }

}

void MainTests::testPublishToItself()
{
    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5);

    client.subscribe("mytopic", 1, false);

    try
    {
        client.publish("mytopic", "mypayload", 1);
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
}

void MainTests::testNoLocalPublishToItself()
{
    FlashMQTestClient client;
    client.start();
    client.connectClient(ProtocolVersion::Mqtt5);

    client.subscribe("mytopic", 1, true);

    try
    {
        client.publish("mytopic", "mypayload", 1);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }

    usleep(1000000);

    MYCASTCOMPARE(client.receivedPublishes.size(), 0);
}

void MainTests::testTopicMatchingInSubscriptionTreeHelper(const std::string &subscribe_topic, const std::string &publish_topic, int match_count)
{
    if (!isValidSubscribePath(subscribe_topic))
        throw std::runtime_error("invalid test: subscribe topic invalid");

    if (!isValidPublishPath(publish_topic))
        throw std::runtime_error("Invalid test: invalid publish topic: " + publish_topic);

    SubscriptionStore store;

    const std::vector<std::string> subscribe_subtopics = splitTopic(subscribe_topic);
    const std::vector<std::string> publish_subtopics = splitTopic(publish_topic);

    std::shared_ptr<ThreadData> td;
    const Settings *settings = ThreadGlobals::getSettings();
    std::shared_ptr<Client> client = std::make_shared<Client>(0, td, nullptr, false, false, nullptr, *settings, false);
    client->setClientProperties(ProtocolVersion::Mqtt5, "mytestclient", "myusername", true, 60);
    store.registerClientAndKickExistingOne(client);

    store.addSubscription(client, subscribe_subtopics, 0, false, false);

    SubscriptionNode *root = &store.root;
    std::forward_list<ReceivingSubscriber> receivers;
    store.publishRecursively(publish_subtopics.begin(), publish_subtopics.end(), root, receivers, 0, "fakeclientid");

    QVERIFY2(std::distance(receivers.begin(), receivers.end()) == match_count, publish_topic.c_str());
}

void MainTests::testTopicMatchingInSubscriptionTree()
{
    testTopicMatchingInSubscriptionTreeHelper("match/this/topic/#", "match/this/topic/wer");
    testTopicMatchingInSubscriptionTreeHelper("match/this/topic/#", "match/this/topic/wer/zxcvzxcv");

    testTopicMatchingInSubscriptionTreeHelper("match/+/topic/#", "match/this/topic/wer/zxcvzxcv");
    testTopicMatchingInSubscriptionTreeHelper("match/+/topic/+", "match/wer/topic/bbb");

    testTopicMatchingInSubscriptionTreeHelper("match/+/topic/+", "match/wer/topic/bbb/eee", 0);
    testTopicMatchingInSubscriptionTreeHelper("match/+/topic/+/wer/#", "match/uwoeirv.m/topic/bbb/eee", 0);

    testTopicMatchingInSubscriptionTreeHelper("#", "match/wer/topic/bbb/eee");
    testTopicMatchingInSubscriptionTreeHelper("+/+/topic/+", "zcccvcv/wer/topic/haha");
    testTopicMatchingInSubscriptionTreeHelper("a/b/c/d/#", "a/b/c/d/e");
    testTopicMatchingInSubscriptionTreeHelper("a/#", "a");
    testTopicMatchingInSubscriptionTreeHelper("a/#", "a/b");
    testTopicMatchingInSubscriptionTreeHelper("/#", "/b");
    testTopicMatchingInSubscriptionTreeHelper("/#", "/b/wer");
    testTopicMatchingInSubscriptionTreeHelper("a/b/c/d/#", "a/b/c/d/e/f");
    testTopicMatchingInSubscriptionTreeHelper("a/b/c/d/#", "a/b/c/", 0);
    testTopicMatchingInSubscriptionTreeHelper("a/b/c/d/#", "a/b/c", 0);

    testTopicMatchingInSubscriptionTreeHelper("a/b/c/d/#", "a/b/c/d");

    // Taken from testTopicsMatch(), but that test will be removed.
    testTopicMatchingInSubscriptionTreeHelper("#", "asdf/b/sdf");
    testTopicMatchingInSubscriptionTreeHelper("#", "/one/two/asdf");
    testTopicMatchingInSubscriptionTreeHelper("#", "/one/two/asdf/");
    testTopicMatchingInSubscriptionTreeHelper("+/+/+/+/+", "/one/two/asdf/");
    testTopicMatchingInSubscriptionTreeHelper("+/+/#", "/one/two/asdf/");
    testTopicMatchingInSubscriptionTreeHelper("+/+/#", "/1234567890abcdef/two/asdf/");
    testTopicMatchingInSubscriptionTreeHelper("+/+/#", "/1234567890abcdefg/two/asdf/");
    testTopicMatchingInSubscriptionTreeHelper("+/+/#", "/1234567890abcde/two/asdf/");
    testTopicMatchingInSubscriptionTreeHelper("+/+/#", "1234567890abcde//two/asdf/");
    testTopicMatchingInSubscriptionTreeHelper("+/santa", "/one/two/asdf/", 0);
    testTopicMatchingInSubscriptionTreeHelper("+/+/+/+/", "/one/two/asdf/a", 0);
    testTopicMatchingInSubscriptionTreeHelper("+/one/+/+/", "/one/two/asdf/a", 0);
}




