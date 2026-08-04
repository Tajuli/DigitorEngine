cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_app_integration_qualification LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_plugin_app_integration_qualification
  main.c
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_app_integration_c.cpp")
target_include_directories(digitor_plugin_app_integration_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
if(WIN32)
  target_compile_definitions(digitor_plugin_app_integration_qualification PRIVATE DIGITOR_ENGINE_STATIC)
endif()
