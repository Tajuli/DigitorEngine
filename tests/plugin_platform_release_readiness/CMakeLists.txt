cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_platform_release_readiness LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_plugin_platform_release_readiness
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_platform_release_readiness.cpp")
target_include_directories(digitor_plugin_platform_release_readiness PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
