QT += testlib
QT -= gui
QT += network
QT += qmqtt

DEFINES += TESTING

INCLUDEPATH += ..

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

TEMPLATE = app

SOURCES +=  tst_maintests.cpp \
    ../cirbuf.cpp \
    ../client.cpp \
    ../exceptions.cpp \
    ../mainapp.cpp \
    ../mqttpacket.cpp \
    ../retainedmessage.cpp \
    ../rwlockguard.cpp \
    ../subscriptionstore.cpp \
    ../threaddata.cpp \
    ../types.cpp \
    ../utils.cpp \
    ../logger.cpp \
    ../authplugin.cpp \
    ../session.cpp \
    ../configfileparser.cpp \
    ../sslctxmanager.cpp \
    ../timer.cpp \
    ../iowrapper.cpp \
    ../mosquittoauthoptcompatwrap.cpp \
    ../settings.cpp \
    ../listener.cpp \
    ../unscopedlock.cpp \
    ../scopedsocket.cpp \
    ../bindaddr.cpp \
    ../oneinstancelock.cpp \
    ../evpencodectxmanager.cpp \
    ../acltree.cpp \
    ../threadlocalutils.cpp \
    mainappthread.cpp \
    twoclienttestcontext.cpp


HEADERS += \
    ../cirbuf.h \
    ../client.h \
    ../exceptions.h \
    ../forward_declarations.h \
    ../mainapp.h \
    ../mqttpacket.h \
    ../retainedmessage.h \
    ../rwlockguard.h \
    ../subscriptionstore.h \
    ../threaddata.h \
    ../types.h \
    ../utils.h \
    ../logger.h \
    ../authplugin.h \
    ../session.h \
    ../configfileparser.h \
    ../sslctxmanager.h \
    ../timer.h \
    ../iowrapper.h \
    ../mosquittoauthoptcompatwrap.h \
    ../settings.h \
    ../listener.h \
    ../unscopedlock.h \
    ../scopedsocket.h \
    ../bindaddr.h \
    ../oneinstancelock.h \
    ../evpencodectxmanager.h \
    ../acltree.h \
    ../threadlocalutils.h \
    mainappthread.h \
    twoclienttestcontext.h

LIBS += -ldl -lssl -lcrypto

QMAKE_LFLAGS += -rdynamic
QMAKE_CXXFLAGS += -msse4.2
