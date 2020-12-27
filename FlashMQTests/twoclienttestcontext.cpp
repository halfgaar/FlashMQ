#include "twoclienttestcontext.h"

#include <QEventLoop>
#include <QTimer>

TwoClientTestContext::TwoClientTestContext(QObject *parent) : QObject(parent)
{
    QHostInfo targetHostInfo = QHostInfo::fromName("localhost");
    QHostAddress targetHost(targetHostInfo.addresses().first());
    sender.reset(new QMQTT::Client(targetHost));
    receiver.reset(new QMQTT::Client(targetHost));
}

void TwoClientTestContext::publishRetained(const QString &topic, const QByteArray &payload)
{
    QMQTT::Message msg;
    msg.setTopic(topic);
    msg.setRetain(true);
    msg.setQos(0);
    msg.setPayload(payload);
    sender->publish(msg);
}

void TwoClientTestContext::connectSender()
{
    sender->connectToHost();
    QEventLoop waiter;
    connect(sender.data(), &QMQTT::Client::connected, &waiter, &QEventLoop::quit);
    waiter.exec();
}

void TwoClientTestContext::connectReceiver()
{
    connect(receiver.data(), &QMQTT::Client::received, this, &TwoClientTestContext::onReceiverReceived);

    receiver->connectToHost();
    QEventLoop waiter;
    connect(receiver.data(), &QMQTT::Client::connected, &waiter, &QEventLoop::quit);
    waiter.exec();
}

void TwoClientTestContext::disconnectReceiver()
{
    receiver->disconnectFromHost();
    QEventLoop waiter;
    connect(sender.data(), &QMQTT::Client::disconnected, &waiter, &QEventLoop::quit);
    waiter.exec();
}

void TwoClientTestContext::subscribeReceiver(const QString &topic)
{
    receiver->subscribe(topic);
}

void TwoClientTestContext::waitReceiverReceived()
{
    QEventLoop waiter;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(1000);
    connect(&timeout, &QTimer::timeout, &waiter, &QEventLoop::quit);
    connect(receiver.data(), &QMQTT::Client::received, &waiter, &QEventLoop::quit);
    timeout.start();
    waiter.exec();
}

void TwoClientTestContext::onReceiverReceived(const QMQTT::Message &message)
{
    receivedMessages.append(message);
}
