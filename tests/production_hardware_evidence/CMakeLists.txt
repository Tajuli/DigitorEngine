cmake_minimum_required(VERSION 3.20)
project(digitor_production_hardware_evidence LANGUAGES CXX)

add_executable(digitor_production_hardware_evidence
  main.cpp
  ../../src/release/production_hardware_evidence.cpp
)
target_include_directories(digitor_production_hardware_evidence PRIVATE ../../include)
target_compile_features(digitor_production_hardware_evidence PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_hardware_evidence PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_hardware_evidence PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME production_hardware_evidence
         COMMAND digitor_production_hardware_evidence)
