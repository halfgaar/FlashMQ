cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0048 NEW)
include(CheckCXXCompilerFlag)
include(../CMakeLists.shared)

project(FlashMQTests VERSION 1.0.0 LANGUAGES CXX)

add_definitions(-DOPENSSL_API_COMPAT=0x10100000L)
add_definitions(-DFLASHMQ_VERSION=\"${PROJECT_VERSION}\")
add_definitions(-DTESTING)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (FMQ_NO_SSE)
    add_definitions(-DFMQ_NO_SSE)
    message("Building tests wihout SSE.")
else()
    SET(CMAKE_CXX_FLAGS "-msse4.2")
endif()

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

add_compile_options(-Wall)

add_library(test_plugin SHARED
    plugins/test_plugin.h plugins/test_plugin.cpp
    plugins/curlfunctions.h plugins/curlfunctions.cpp
)

target_link_libraries(test_plugin curl)

# Copying to a version 0.0.1 file is a bit of a hack. I'm not sure how to version it in this CMake otherwise.
add_custom_command(TARGET test_plugin POST_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory "plugins/")
add_custom_command(TARGET test_plugin POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_plugin> "plugins/libtest_plugin.so.0.0.1")

# Copy in a way that always copies, to avoid unexpected staleness.
add_custom_command(TARGET test_plugin POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/UTF-8-test.txt"
    "${CMAKE_BINARY_DIR}/UTF-8-test.txt")
add_custom_command(TARGET test_plugin POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/../fuzztests/plainwebsocketpacket1_handshake.dat"
    "${CMAKE_BINARY_DIR}/plainwebsocketpacket1_handshake.dat")

add_executable(flashmq-tests
    ${FLASHMQ_HEADERS}
    ${FLASHMQ_IMPLS}

    ../flashmqtestclient.cpp ../flashmqtestclient.h
    main.cpp
    mainappinthread.h mainappinthread.cpp
    maintests.h maintests.cpp
    testhelpers.cpp testhelpers.h
    flashmqtempdir.h flashmqtempdir.cpp
    tst_maintests.cpp
    retaintests.cpp
    conffiletemp.cpp conffiletemp.h
    filecloser.cpp filecloser.h
    plugintests.cpp
    configtests.cpp
    sharedsubscriptionstests.cpp
    websockettests.cpp
    willtests.cpp
    dnstests.cpp
    testinitializer.h testinitializer.cpp

    )

target_include_directories(flashmq-tests PUBLIC ..)

target_link_libraries(flashmq-tests pthread dl ssl crypto resolv anl)





