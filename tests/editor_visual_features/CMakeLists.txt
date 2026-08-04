cmake_minimum_required(VERSION 3.20)
project(digitor_editor_visual_features LANGUAGES CXX)

add_executable(digitor_editor_visual_features
  main.cpp
  ../../src/editor/editor_visual_features.cpp)

target_include_directories(digitor_editor_visual_features PRIVATE ../../include)
target_compile_features(digitor_editor_visual_features PRIVATE cxx_std_20)
