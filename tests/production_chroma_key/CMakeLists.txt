cmake_minimum_required(VERSION 3.20)
project(digitor_production_chroma_key LANGUAGES CXX)
add_executable(digitor_production_chroma_key
  main.cpp
  ../../src/gpu/production_chroma_key.cpp)
target_include_directories(digitor_production_chroma_key PRIVATE ../../include)
target_compile_features(digitor_production_chroma_key PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_production_chroma_key PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_chroma_key PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
