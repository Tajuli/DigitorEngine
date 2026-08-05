cmake_minimum_required(VERSION 3.20)
project(digitor_production_time_remap LANGUAGES CXX)
add_executable(digitor_production_time_remap
  main.cpp
  ../../src/timeline/production_time_remap.cpp)
target_include_directories(digitor_production_time_remap PRIVATE ../../include)
target_compile_features(digitor_production_time_remap PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_production_time_remap PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_time_remap PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
