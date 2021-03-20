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

    MainAppThread mainApp;

public:
    MainTests();
    ~MainTests();

private slots:
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

};

MainTests::MainTests()
{
    mainApp.start();
    mainApp.waitForStarted();
}

MainTests::~MainTests()
{

}

void MainTests::cleanupTestCase()
{
    mainApp.stopApp();
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
    testContext.waitReceiverReceived();

    QVERIFY2(testContext.receivedMessages.count() == 1, "There must be one message in the received list");

    QMQTT::Message msg = testContext.receivedMessages.first();
    QCOMPARE(msg.payload(), payload);
    QVERIFY(msg.retain());

    testContext.receivedMessages.clear();

    testContext.publish(topic, payload, true);
    testContext.waitReceiverReceived();

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
    testContext.waitReceiverReceived();

    QVERIFY2(testContext.receivedMessages.count() == 1, "There must be one message in the received list");

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
    testContext.waitReceiverReceived();

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
    testContext.waitReceiverReceived();

    QVERIFY2(testContext.receivedMessages.count() == 1, "There must be one message in the received list");

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
    testContext.waitReceiverReceived();

    QCOMPARE(testContext.receivedMessages.count(), 1);

    QMQTT::Message msg = testContext.receivedMessages.first();
    QCOMPARE(msg.payload(), payload);
    QVERIFY(!msg.retain());
}

QTEST_GUILESS_MAIN(MainTests)

#include "tst_maintests.moc"
