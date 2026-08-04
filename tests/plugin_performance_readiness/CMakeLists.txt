cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_performance_readiness LANGUAGES CXX)
add_executable(digitor_plugin_performance_readiness
  main.cpp
  ../../src/plugin/plugin_performance_readiness.cpp)
target_include_directories(digitor_plugin_performance_readiness PRIVATE ../../include)
target_compile_features(digitor_plugin_performance_readiness PRIVATE cxx_std_20)
