cmake_minimum_required(VERSION 3.20)
project(production_evidence_capture_test LANGUAGES CXX)

add_executable(production_evidence_capture_test
  main.cpp
  ../../src/release/production_evidence_capture.cpp)

target_include_directories(production_evidence_capture_test PRIVATE ../../include)
target_compile_features(production_evidence_capture_test PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(production_evidence_capture_test PRIVATE /W4 /WX)
else()
  target_compile_options(production_evidence_capture_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME production_evidence_capture_test
         COMMAND production_evidence_capture_test)
