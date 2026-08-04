cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_backend_pass_recorders_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_plugin_backend_pass_recorders_qualification
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_backend_pass_recorders.cpp")
target_include_directories(digitor_plugin_backend_pass_recorders_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
