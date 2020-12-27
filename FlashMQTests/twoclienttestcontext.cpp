#include "twoclienttestcontext.h"

#include <QEventLoop>
#include <QTimer>

// TODO: port to QMqttClient that newer Qts now have?

TwoClientTestContext::TwoClientTestContext(QObject *parent) : QObject(parent)
{
    QHostInfo targetHostInfo = QHostInfo::fromName("localhost");
    QHostAddress targetHost(targetHostInfo.addresses().first());
    sender.reset(new QMQTT::Client(targetHost));
    receiver.reset(new QMQTT::Client(targetHost));

    connect(sender.data(), &QMQTT::Client::error, this, &TwoClientTestContext::onClientError);
    connect(receiver.data(), &QMQTT::Client::error, this, &TwoClientTestContext::onClientError);
}

void TwoClientTestContext::publish(const QString &topic, const QByteArray &payload, bool retain)
{
    QMQTT::Message msg;
    msg.setTopic(topic);
    msg.setRetain(retain);
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

void TwoClientTestContext::onClientError(const QMQTT::ClientError error)
{
    const QMQTT::Client *_sender = sender.data();

    // TODO: arg, doesn't qmqtt have a better way for this?
    QString errStr = QString("unknown error");
    if (error == QMQTT::SocketConnectionRefusedError)
        errStr = "Connection refused";
    if (error == QMQTT::SocketRemoteHostClosedError)
        errStr = "Remote host closed";
    if (error == QMQTT::SocketHostNotFoundError)
        errStr = "Remote host not found";
    if (error == QMQTT::MqttBadUserNameOrPasswordError)
        errStr = "MQTT bad user or password";
    if (error == QMQTT::MqttNotAuthorizedError)
        errStr = "MQTT not authorized";
    if (error == QMQTT::SocketResourceError)
        errStr = "Socket resource error. Is your OS limiting you? Ulimit, etc?";
    if (error == QMQTT::SocketSslInternalError)
        errStr = "Socket SSL internal error.";
    if (error == QMQTT::SocketTimeoutError)
        errStr = "Socket timeout";

    QString msg = QString("Client %1 error code: %2 (%3). Initiated delayed reconnect.\n").arg(_sender->clientId()).arg(error).arg(errStr);
    throw new std::runtime_error(msg.toStdString());
}

void TwoClientTestContext::onReceiverReceived(const QMQTT::Message &message)
{
    receivedMessages.append(message);
}
