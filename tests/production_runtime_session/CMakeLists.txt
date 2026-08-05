cmake_minimum_required(VERSION 3.20)
project(DigitorProductionRuntimeSession LANGUAGES CXX)

add_executable(digitor_production_runtime_session
  main.cpp
  ../../src/runtime/production_runtime_session.cpp
)

target_include_directories(digitor_production_runtime_session PRIVATE ../../include)
target_compile_features(digitor_production_runtime_session PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_runtime_session PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_runtime_session PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()