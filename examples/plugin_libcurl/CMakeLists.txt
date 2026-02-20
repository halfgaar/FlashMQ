cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0048 NEW)

project(plugin_libcurl VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_compile_options(-Wall)

add_library(plugin_libcurl SHARED
  vendor/flashmq_plugin.h
  vendor/flashmq_public.h

  src/pluginstate.h
  src/curl_functions.h
  src/authenticatingclient.h

  src/authenticatingclient.cpp
  src/curl_functions.cpp
  src/pluginstate.cpp

  src/plugin_libcurl.cpp
)

target_include_directories(plugin_libcurl PUBLIC . src)
target_link_libraries(plugin_libcurl curl)

set_target_properties(plugin_libcurl PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(plugin_libcurl PROPERTIES SOVERSION 1)
