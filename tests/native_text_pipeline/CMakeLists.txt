cmake_minimum_required(VERSION 3.20)
project(digitor_native_text_pipeline LANGUAGES CXX)
add_executable(digitor_native_text_pipeline
  main.cpp
  ../../src/text/native_text_pipeline.cpp)
target_include_directories(digitor_native_text_pipeline PRIVATE ../../include)
target_compile_features(digitor_native_text_pipeline PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_native_text_pipeline PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(digitor_native_text_pipeline PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
