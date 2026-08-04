cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_install_store_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_plugin_install_store_qualification
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_install_store.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_parameter_schema.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/remote_plugin_marketplace.cpp")
target_include_directories(digitor_plugin_install_store_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
