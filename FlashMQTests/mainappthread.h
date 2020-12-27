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

public slots:
    void run() override;
    void stopApp();
    void waitForStarted();

signals:

};

#endif // MAINAPPTHREAD_H
