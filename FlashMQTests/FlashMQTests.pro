QT += testlib
QT -= gui
QT += network

DEFINES += TESTING \
           "FLASHMQ_VERSION=\\\"0.0.0\\\""

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
    ../flashmq_plugin.cpp \
    ../retainedmessagesdb.cpp \
    ../persistencefile.cpp \
    ../sessionsandsubscriptionsdb.cpp \
    ../qospacketqueue.cpp \
    ../threadglobals.cpp \
    ../threadloop.cpp \
    ../publishcopyfactory.cpp \
    ../variablebyteint.cpp \
    ../mqtt5properties.cpp \
    ../globalstats.cpp \
    ../derivablecounter.cpp \
    ../packetdatatypes.cpp \
    ../flashmqtestclient.cpp \
    mainappthread.cpp


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
    ../flashmq_plugin.h \
    ../retainedmessagesdb.h \
    ../persistencefile.h \
    ../sessionsandsubscriptionsdb.h \
    ../qospacketqueue.h \
    ../threadglobals.h \
    ../threadloop.h \
    ../publishcopyfactory.h \
    ../variablebyteint.h \
    ../mqtt5properties.h \
    ../globalstats.h \
    ../derivablecounter.h \
    ../packetdatatypes.h \
    ../flashmqtestclient.h \
    mainappthread.h

LIBS += -ldl -lssl -lcrypto

QMAKE_LFLAGS += -rdynamic
QMAKE_CXXFLAGS += -msse4.2
