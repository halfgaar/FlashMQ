cmake_minimum_required(VERSION 3.5)

project(FlashMQ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(CMAKE_CXX_FLAGS "-rdynamic")

add_executable(FlashMQ
    mainapp.cpp
    main.cpp
    utils.cpp
    threaddata.cpp
    client.cpp
    session.cpp
    bytestopacketparser.cpp
    mqttpacket.cpp
    exceptions.cpp
    types.cpp
    subscriptionstore.cpp
    rwlockguard.cpp
    retainedmessage.cpp
    cirbuf.cpp
    logger.cpp
    authplugin.cpp
    configfileparser.cpp
    )

target_link_libraries(FlashMQ pthread dl)
