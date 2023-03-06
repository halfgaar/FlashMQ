cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0048 NEW)

project(plugin_libcurl VERSION 1.0.0 LANGUAGES CXX)

add_compile_options(-Wall)

add_library(plugin_libcurl SHARED
  pluginstate.h
  curl_functions.h
  authenticatingclient.h

  authenticatingclient.cpp
  curl_functions.cpp
  pluginstate.cpp

  plugin_libcurl.cpp
)

target_link_libraries(plugin_libcurl curl)

set_target_properties(plugin_libcurl PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(plugin_libcurl PROPERTIES SOVERSION 1)
