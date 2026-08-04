cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_compatibility_lts LANGUAGES CXX)

add_executable(digitor_plugin_compatibility_lts
  main.cpp
  ../../src/plugin/plugin_compatibility_lts.cpp
)
target_include_directories(digitor_plugin_compatibility_lts PRIVATE ../../include)
target_compile_features(digitor_plugin_compatibility_lts PRIVATE cxx_std_20)
