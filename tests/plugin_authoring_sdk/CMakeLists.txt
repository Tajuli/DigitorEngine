cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_authoring_sdk_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_plugin_authoring_sdk_qualification
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_authoring_sdk.cpp")
target_include_directories(digitor_plugin_authoring_sdk_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
add_executable(digitor_plugin_author
  "${CMAKE_CURRENT_LIST_DIR}/../../tools/digitor_plugin_author/main.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_authoring_sdk.cpp")
target_include_directories(digitor_plugin_author PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
