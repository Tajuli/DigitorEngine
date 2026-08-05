cmake_minimum_required(VERSION 3.20)
project(digitor_production_release_candidate LANGUAGES CXX)

add_executable(digitor_production_release_candidate
  main.cpp
  ../../src/release/production_release_candidate.cpp
)
target_include_directories(digitor_production_release_candidate PRIVATE ../../include)
target_compile_features(digitor_production_release_candidate PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_production_release_candidate PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_release_candidate PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
