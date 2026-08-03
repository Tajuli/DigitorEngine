cmake_minimum_required(VERSION 3.20)
project(digitor_remote_plugin_marketplace_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_remote_plugin_marketplace_qualification
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/remote_plugin_marketplace.cpp")
target_include_directories(digitor_remote_plugin_marketplace_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
