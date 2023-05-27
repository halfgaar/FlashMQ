/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
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
