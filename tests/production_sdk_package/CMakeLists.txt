cmake_minimum_required(VERSION 3.20)
project(digitor_production_sdk_package LANGUAGES CXX)

add_executable(digitor_production_sdk_package
  main.cpp
  ../../src/runtime/production_sdk_package.cpp)

target_include_directories(digitor_production_sdk_package PRIVATE ../../include)
target_compile_features(digitor_production_sdk_package PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_sdk_package PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_sdk_package PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()