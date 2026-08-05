cmake_minimum_required(VERSION 3.20)
project(DigitorProductionTransitions LANGUAGES CXX)

add_executable(digitor_production_transitions
  main.cpp
  ../../src/gpu/production_transitions.cpp
)

target_include_directories(digitor_production_transitions PRIVATE ../../include)
target_compile_features(digitor_production_transitions PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_transitions PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_transitions PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
