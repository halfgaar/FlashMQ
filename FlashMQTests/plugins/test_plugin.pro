QT -= gui

CONFIG += c++17

TARGET = test_plugin
TEMPLATE = lib

VERSION=0.0.1

LIBS += -lcurl

HEADERS += test_plugin.h \
    curlfunctions.h

SOURCES += test_plugin.cpp \
    curlfunctions.cpp
