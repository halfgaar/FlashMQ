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

#ifndef MAINAPPTHREAD_H
#define MAINAPPTHREAD_H

#include <QObject>
#include <QThread>
#include <mainapp.h>

class MainAppThread : public QThread
{
    Q_OBJECT
    MainApp *appInstance = nullptr;
public:
    explicit MainAppThread(QObject *parent = nullptr);
    MainAppThread(const std::vector<std::string> &args, QObject *parent = nullptr);
    ~MainAppThread();

public slots:
    void run() override;
    void stopApp();
    void waitForStarted();
    std::shared_ptr<SubscriptionStore> getStore();

signals:

};

#endif // MAINAPPTHREAD_H
