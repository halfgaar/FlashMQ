cmake_minimum_required(VERSION 3.5)
include(CheckCXXCompilerFlag)

project(FlashMQ LANGUAGES CXX)

add_definitions(-DOPENSSL_API_COMPAT=0x10100000L)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(CMAKE_CXX_FLAGS "-msse4.2")

check_cxx_compiler_flag("-rdynamic" COMPILER_SUPPORTS_RDYNAMIC)
if (${COMPILER_SUPPORTS_RDYNAMIC})
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
endif()

add_compile_options(-Wall)

add_executable(FlashMQ
    forward_declarations.h
    mainapp.h
    utils.h
    threaddata.h
    client.h
    session.h
    mqttpacket.h
    exceptions.h
    types.h
    subscriptionstore.h
    rwlockguard.h
    retainedmessage.h
    cirbuf.h
    logger.h
    authplugin.h
    configfileparser.h
    sslctxmanager.h
    timer.h
    iowrapper.h
    mosquittoauthoptcompatwrap.h
    settings.h
    listener.h
    unscopedlock.h
    scopedsocket.h
    bindaddr.h
    oneinstancelock.h
    evpencodectxmanager.h
    acltree.h
    enums.h
    threadlocalutils.h
    flashmq_plugin.h
    retainedmessagesdb.h
    persistencefile.h
    sessionsandsubscriptionsdb.h
    qospacketqueue.h
    threadauth.h

    mainapp.cpp
    main.cpp
    utils.cpp
    threaddata.cpp
    client.cpp
    session.cpp
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
    sslctxmanager.cpp
    timer.cpp
    iowrapper.cpp
    mosquittoauthoptcompatwrap.cpp
    settings.cpp
    listener.cpp
    unscopedlock.cpp
    scopedsocket.cpp
    bindaddr.cpp
    oneinstancelock.cpp
    evpencodectxmanager.cpp
    acltree.cpp
    threadlocalutils.cpp
    flashmq_plugin.cpp
    retainedmessagesdb.cpp
    persistencefile.cpp
    sessionsandsubscriptionsdb.cpp
    qospacketqueue.cpp
    threadauth.cpp

    )

target_link_libraries(FlashMQ pthread dl ssl crypto)
