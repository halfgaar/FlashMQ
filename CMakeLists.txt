cmake_minimum_required(VERSION 3.5)

project(FlashMQ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_executable(FlashMQ main.cpp utils.cpp threaddata.cpp)

target_link_libraries(FlashMQ pthread)
