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

#include "twoclienttestcontext.h"

#include <QEventLoop>
#include <QTimer>

// TODO: port to QMqttClient that newer Qts now have?

TwoClientTestContext::TwoClientTestContext(int clientNr, QObject *parent) : QObject(parent)
{
    QHostInfo targetHostInfo = QHostInfo::fromName("localhost");
    QHostAddress targetHost(targetHostInfo.addresses().first());
    sender.reset(new QMQTT::Client(targetHost));
    sender->setClientId(QString("Sender%1").arg(clientNr));
    receiver.reset(new QMQTT::Client(targetHost));
    receiver->setClientId(QString("Receiver%1").arg(clientNr));

    connect(sender.data(), &QMQTT::Client::error, this, &TwoClientTestContext::onClientError);
    connect(receiver.data(), &QMQTT::Client::error, this, &TwoClientTestContext::onClientError);
}

void TwoClientTestContext::publish(const QString &topic, const QByteArray &payload)
{
    publish(topic, payload, 0, false);
}

void TwoClientTestContext::publish(const QString &topic, const QByteArray &payload, bool retain)
{
    publish(topic, payload, 0, retain);
}

void TwoClientTestContext::publish(const QString &topic, const QByteArray &payload, const quint8 qos, bool retain)
{
    QMQTT::Message msg;
    msg.setTopic(topic);
    msg.setRetain(retain);
    msg.setQos(qos);
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

void TwoClientTestContext::subscribeReceiver(const QString &topic, const quint8 qos)
{
    receiver->subscribe(topic, qos);

    QEventLoop waiter;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(1000);
    connect(&timeout, &QTimer::timeout, &waiter, &QEventLoop::quit);
    connect(receiver.data(), &QMQTT::Client::subscribed, &waiter, &QEventLoop::quit);
    timeout.start();
    waiter.exec();
}

void TwoClientTestContext::waitReceiverReceived(int count)
{
    if (count > 0 && receivedMessages.count() == count)
        return;

    QEventLoop waiter;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(3000);
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
    throw std::runtime_error(msg.toStdString());
}

void TwoClientTestContext::onReceiverReceived(const QMQTT::Message &message)
{
    receivedMessages.append(message);
}
