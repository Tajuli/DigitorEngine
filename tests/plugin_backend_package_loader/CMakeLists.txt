cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_backend_package_loader_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_plugin_backend_package_loader_qualification
  main.cpp
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_gpu_program.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../src/plugin/plugin_backend_package_loader.cpp")
target_include_directories(digitor_plugin_backend_package_loader_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
