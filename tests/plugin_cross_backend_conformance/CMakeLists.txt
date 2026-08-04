cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_cross_backend_conformance LANGUAGES CXX)
add_executable(digitor_plugin_cross_backend_conformance
  main.cpp
  ../../src/plugin/plugin_cross_backend_conformance.cpp)
target_include_directories(digitor_plugin_cross_backend_conformance PRIVATE ../../include)
target_compile_features(digitor_plugin_cross_backend_conformance PRIVATE cxx_std_20)
