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

#ifndef RETAINTESTCONTEXT_H
#define RETAINTESTCONTEXT_H

#include <QObject>
#include <QtQmqtt/qmqtt.h>
#include <QHostInfo>

class TwoClientTestContext : public QObject
{
    Q_OBJECT

    QScopedPointer<QMQTT::Client> sender;
    QScopedPointer<QMQTT::Client> receiver;

private slots:
    void onReceiverReceived(const QMQTT::Message& message);

public:
    explicit TwoClientTestContext(QObject *parent = nullptr);
    void publish(const QString &topic, const QByteArray &payload, bool retain = false);
    void connectSender();
    void connectReceiver();
    void disconnectReceiver();
    void subscribeReceiver(const QString &topic);
    void waitReceiverReceived(int count);
    void onClientError(const QMQTT::ClientError error);

    QList<QMQTT::Message> receivedMessages;

signals:

};

#endif // RETAINTESTCONTEXT_H
