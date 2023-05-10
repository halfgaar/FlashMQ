# This is the main .pro file for the tests, which also builds plugin(s)

TEMPLATE = subdirs

SUBDIRS = plugins/test_plugin.pro \
          FlashMQTests.pro

CONFIG += ordered c++17

contains(DEFINES, FMQ_NO_SSE) {
   message(Building tests wihout SSE.)
}
