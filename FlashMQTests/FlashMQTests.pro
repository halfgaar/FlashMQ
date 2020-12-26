QT += testlib
QT -= gui
Qt += network

DEFINES += TESTING

INCLUDEPATH += ..

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

TEMPLATE = app

SOURCES +=  tst_maintests.cpp \
    ../cirbuf.cpp

HEADERS += \
    ../cirbuf.h
