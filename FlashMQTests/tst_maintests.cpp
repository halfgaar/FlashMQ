/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, version 3.

FlashMQ is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with FlashMQ. If not, see <https://www.gnu.org/licenses/>.
*/

#include <QtTest>


#include <QtQmqtt/qmqtt.h>
#include <QScopedPointer>
#include <QHostInfo>

#include "cirbuf.h"
#include "mainapp.h"
#include "mainappthread.h"
#include "twoclienttestcontext.h"
#include "threadlocalutils.h"

// Dumb Qt version gives warnings when comparing uint with number literal.
template <typename T1, typename T2>
inline bool myCastCompare(const T1 &t1, const T2 &t2, const char *actual, const char *expected,
                      const char *file, int line)
{
    T1 t2_ = static_cast<T1>(t2);
    return QTest::compare_helper(t1 == t2_, "Compared values are not the same",
                                 QTest::toString(t1), QTest::toString(t2), actual, expected, file, line);
}

#define MYCASTCOMPARE(actual, expected) \
do {\
    if (!myCastCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return;\
} while (false)

class MainTests : public QObject
{
    Q_OBJECT

    QScopedPointer<MainAppThread> mainApp;

public:
    MainTests();
    ~MainTests();

private slots:
    void init();
    void cleanup();

    void cleanupTestCase();

    void test_circbuf();
    void test_circbuf_unwrapped_doubling();
    void test_circbuf_wrapped_doubling();
    void test_circbuf_full_wrapped_buffer_doubling();

    void test_validSubscribePath();

    void test_retained();
    void test_retained_changed();
    void test_retained_removed();

    void test_packet_bigger_than_one_doubling();
    void test_very_big_packet();

    void test_acl_tree();
    void test_acl_tree2();
    void test_acl_patterns_username();
    void test_acl_patterns_clientid();

    void test_sse_split();

    void test_validUtf8Generic();
    void test_validUtf8Sse();

    void testPacketInt16Parse();

    void testTopicsMatch();

};

MainTests::MainTests()
{

}

MainTests::~MainTests()
{

}

void MainTests::init()
{
    mainApp.reset();
    mainApp.reset(new MainAppThread());
    mainApp->start();
    mainApp->waitForStarted();
}

void MainTests::cleanup()
{
    mainApp->stopApp();
}

void MainTests::cleanupTestCase()
{

}

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
    QCOMPARE(buf.buf[64], 0); // Vacant place, because of the circulerness.

    MYCASTCOMPARE(buf.head, 63);

    buf.doubleSize();
    tail = buf.tailPtr();

    for (int i = 0; i < w; i++)
    {
        QCOMPARE(tail[i], i+1);
    }

    for (int i = 63; i < 128; i++)
    {
        QCOMPARE(tail[i], 5);
    }

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

void MainTests::test_retained()
{
    TwoClientTestContext testContext;

    QByteArray payload = "We are testing";
    QString topic = "retaintopic";

    testContext.connectSender();
    testContext.publish(topic, payload, true);
    testContext.publish("dummy2", "Nobody sees this", true);

    testContext.connectReceiver();
    testContext.subscribeReceiver("dummy");
    testContext.subscribeReceiver(topic);
    testContext.waitReceiverReceived(1);

    QCOMPARE(testContext.receivedMessages.count(), 1);

    QMQTT::Message msg = testContext.receivedMessages.first();
    QCOMPARE(msg.payload(), payload);
    QVERIFY(msg.retain());

    testContext.receivedMessages.clear();

    testContext.publish(topic, payload, true);
    testContext.waitReceiverReceived(1);

    QVERIFY2(testContext.receivedMessages.count() == 1, "There must be one message in the received list");
    QMQTT::Message msg2 = testContext.receivedMessages.first();
    QCOMPARE(msg2.payload(), payload);
    QVERIFY2(!msg2.retain(), "Getting a retained message while already being subscribed must be marked as normal, not retain.");
}

void MainTests::test_retained_changed()
{
    TwoClientTestContext testContext;

    QByteArray payload = "We are testing";
    QString topic = "retaintopic";

    testContext.connectSender();
    testContext.publish(topic, payload, true);

    payload = "Changed payload";

    testContext.publish(topic, payload, true);

    testContext.connectReceiver();
    testContext.subscribeReceiver(topic);
    testContext.waitReceiverReceived(1);

    QCOMPARE(testContext.receivedMessages.count(), 1);

    QMQTT::Message msg = testContext.receivedMessages.first();
    QCOMPARE(msg.payload(), payload);
    QVERIFY(msg.retain());
}

void MainTests::test_retained_removed()
{
    TwoClientTestContext testContext;

    QByteArray payload = "We are testing";
    QString topic = "retaintopic";

    testContext.connectSender();
    testContext.publish(topic, payload, true);

    payload = "";

    testContext.publish(topic, payload, true);

    testContext.connectReceiver();
    testContext.subscribeReceiver(topic);
    testContext.waitReceiverReceived(0);

    QVERIFY2(testContext.receivedMessages.empty(), "We erased the retained message. We shouldn't have received any.");
}

void MainTests::test_packet_bigger_than_one_doubling()
{
    TwoClientTestContext testContext;

    QByteArray payload(8000, 3);
    QString topic = "hugepacket";

    testContext.connectSender();
    testContext.connectReceiver();
    testContext.subscribeReceiver(topic);

    testContext.publish(topic, payload);
    testContext.waitReceiverReceived(1);

    QCOMPARE(testContext.receivedMessages.count(), 1);

    QMQTT::Message msg = testContext.receivedMessages.first();
    QCOMPARE(msg.payload(), payload);
    QVERIFY(!msg.retain());
}

// This tests our write buffer, and that it's emptied during writing already.
void MainTests::test_very_big_packet()
{
    TwoClientTestContext testContext;

    QByteArray payload(10*1024*1024, 3);
    QString topic = "hugepacket";

    testContext.connectSender();
    testContext.connectReceiver();
    testContext.subscribeReceiver(topic);

    testContext.publish(topic, payload);
    testContext.waitReceiverReceived(1);

    QCOMPARE(testContext.receivedMessages.count(), 1);

    QMQTT::Message msg = testContext.receivedMessages.first();
    QCOMPARE(msg.payload(), payload);
    QVERIFY(!msg.retain());
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

    QCOMPARE(aclTree.findPermission(splitToVector("d/Jheronimus/f", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Jheronimus/f/A", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Jheronimus/f/A/B", '/'), AclGrant::Read, "Jheronimus", "clientid"), AuthResult::success);

    // Repeat the test, but now with a user for which there is also an unrelated user specific ACL.

    QCOMPARE(aclTree.findPermission(splitToVector("one/Rembrandt/three", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("one/Theo/three", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("a/Rembrandt/c", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("a/NotRembrandt/c", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("a/Rembrandt/c", '/'), AclGrant::Write, "Rembrandt", "clientid"), AuthResult::acl_denied);

    QCOMPARE(aclTree.findPermission(splitToVector("d/Rembrandt/f", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Rembrandt/f/A", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/Rembrandt/f/A/B", '/'), AclGrant::Read, "Rembrandt", "clientid"), AuthResult::success);

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

    QCOMPARE(aclTree.findPermission(splitToVector("d/clientid_one/f", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::acl_denied);
    QCOMPARE(aclTree.findPermission(splitToVector("d/clientid_one/f/A", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::success);
    QCOMPARE(aclTree.findPermission(splitToVector("d/clientid_one/f/A/B", '/'), AclGrant::Read, "foo", "clientid_one"), AuthResult::success);
}

void MainTests::test_sse_split()
{
    SimdUtils data;
    std::vector<std::string> output;

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
        data.splitTopic(t, output);
        QCOMPARE(output, splitToVector(t, '/'));
    }
}

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
    QVERIFY(!data.isValidUtf8("$SYS/asdfasdfasdf", true));

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

void MainTests::testPacketInt16Parse()
{
    std::vector<uint64_t> tests {128, 300, 64, 65550, 32000};

    for (const uint16_t id : tests)
    {
        Publish pub("hallo", "content", 1);
        MqttPacket packet(pub);
        packet.setPacketId(id);
        packet.pos -= 2;
        uint16_t idParsed = packet.readTwoBytesToUInt16();
        QVERIFY(id == idParsed);
    }
}

void MainTests::testTopicsMatch()
{
    QVERIFY(topicsMatch("#", ""));
    QVERIFY(topicsMatch("#", "asdf/b/sdf"));
    QVERIFY(topicsMatch("#", "+/b/sdf"));
    QVERIFY(topicsMatch("#", "/one/two/asdf"));
    QVERIFY(topicsMatch("#", "/one/two/asdf/"));
    QVERIFY(topicsMatch("+/+/+/+/+", "/one/two/asdf/"));
    QVERIFY(topicsMatch("+/+/#", "/one/two/asdf/"));
    QVERIFY(topicsMatch("+/+/#", "/1234567890abcdef/two/asdf/"));
    QVERIFY(topicsMatch("+/+/#", "/1234567890abcdefg/two/asdf/"));
    QVERIFY(topicsMatch("+/+/#", "/1234567890abcde/two/asdf/"));
    QVERIFY(topicsMatch("+/+/#", "1234567890abcde//two/asdf/"));

    QVERIFY(!topicsMatch("+/santa", "/one/two/asdf/"));
    QVERIFY(!topicsMatch("+/+/+/+/", "/one/two/asdf/a"));
    QVERIFY(!topicsMatch("+/one/+/+/", "/one/two/asdf/a"));

    QVERIFY(topicsMatch("$SYS/cow", "$SYS/cow"));
    QVERIFY(topicsMatch("$SYS/cow/+", "$SYS/cow/bla"));
    QVERIFY(topicsMatch("$SYS/#", "$SYS/broker/clients/connected"));

    QVERIFY(!topicsMatch("$SYS/cow/+", "$SYS/cow/bla/foobar"));
    QVERIFY(!topicsMatch("#", "$SYS/cow"));

}

QTEST_GUILESS_MAIN(MainTests)

#include "tst_maintests.moc"
