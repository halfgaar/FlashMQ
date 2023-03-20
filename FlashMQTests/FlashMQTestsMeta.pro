# This is the main .pro file for the tests, which also builds plugin(s)

TEMPLATE = subdirs

SUBDIRS = plugins/test_plugin.pro \
          FlashMQTests.pro

CONFIG += ordered c++17

