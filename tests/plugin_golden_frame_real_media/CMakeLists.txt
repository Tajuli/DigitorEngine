cmake_minimum_required(VERSION 3.20)
project(digitor_plugin_golden_frame_real_media LANGUAGES CXX)

add_executable(digitor_plugin_golden_frame_real_media
  main.cpp
  ../../src/plugin/plugin_golden_frame_qualification.cpp)

target_include_directories(digitor_plugin_golden_frame_real_media PRIVATE ../../include)
target_compile_features(digitor_plugin_golden_frame_real_media PRIVATE cxx_std_20)
