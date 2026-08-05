cmake_minimum_required(VERSION 3.20)
project(digitor_production_resilience_release LANGUAGES CXX)

add_executable(digitor_production_resilience_release
  main.cpp
  ../../src/runtime/production_resilience_release.cpp
)

target_include_directories(digitor_production_resilience_release PRIVATE ../../include)
target_compile_features(digitor_production_resilience_release PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_resilience_release PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_resilience_release PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
