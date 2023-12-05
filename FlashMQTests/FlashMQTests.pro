QT += testlib
QT -= gui
QT += network

DEFINES += TESTING \
           "FLASHMQ_VERSION=\\\"0.0.0\\\""

INCLUDEPATH += ..

CONFIG += qt console warn_on depend_includepath testcase c++17
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
    ../plugin.cpp \
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
    ../haproxy.cpp \
    ../network.cpp \
    ../subscription.cpp \
    ../sharedsubscribers.cpp \
    ../pluginloader.cpp \
    ../queuedtasks.cpp \
    ../acksender.cpp \
    ../bridgeconfig.cpp \
    ../dnsresolver.cpp \
    ../bridgeinfodb.cpp \
    ../globber.cpp \
    ../x509manager.cpp \
    ../backgroundworker.cpp \
    conffiletemp.cpp \
    dnstests.cpp \
    filecloser.cpp \
    flashmqtempdir.cpp \
    mainappthread.cpp \
    plugintests.cpp \
    retaintests.cpp \
    sharedsubscriptionstests.cpp \
    websockettests.cpp \
    willtests.cpp


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
    ../plugin.h \
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
    ../haproxy.h \
    ../network.h \
    ../subscription.h \
    ../sharedsubscribers.h \
    ../pluginloader.h \
    ../queuedtasks.h \
    ../acksender.h \
    ../bridgeconfig.h \
    ../dnsresolver.h \
    ../bridgeinfodb.h \
    ../globber.h \
    ../x509manager.h \
    ../backgroundworker.h \
    conffiletemp.h \
    filecloser.h \
    flashmqtempdir.h \
    mainappthread.h \
    tst_maintests.h

LIBS += -ldl -lssl -lcrypto -lresolv -lanl

QMAKE_LFLAGS += -rdynamic

contains(DEFINES, FMQ_NO_SSE) {
   message(Building tests wihout SSE.)
}
else
{
    QMAKE_CXXFLAGS += -msse4.2
}

copydata.commands = $(COPY_DIR) $$PWD/UTF-8-test.txt $$PWD/../fuzztests/plainwebsocketpacket1_handshake.dat $$OUT_PWD
first.depends = $(first) copydata
export(first.depends)
export(copydata.commands)
QMAKE_EXTRA_TARGETS += first copydata
