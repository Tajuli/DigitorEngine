cmake_minimum_required(VERSION 3.20)
project(digitor_unified_plugin_runtime_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_unified_plugin_runtime_qualification
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/unified_plugin_runtime.cpp")
target_include_directories(digitor_unified_plugin_runtime_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
