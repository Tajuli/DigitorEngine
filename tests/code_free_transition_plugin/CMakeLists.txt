cmake_minimum_required(VERSION 3.20)
project(digitor_code_free_transition_plugin_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_code_free_transition_plugin_qualification
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_parameter_schema.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/remote_plugin_marketplace.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_gpu_program.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_transition_runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_transition_program.cpp")
target_include_directories(digitor_code_free_transition_plugin_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
