cmake_minimum_required(VERSION 3.20)
project(DigitorTransitionSubsystem LANGUAGES CXX)

add_executable(digitor_transition_subsystem_qualification
  main.cpp
  ../../src/gpu/production_transitions.cpp
  ../../src/plugin/plugin_transition_runtime.cpp
  ../../src/plugin/transition_subsystem.cpp
)

target_include_directories(digitor_transition_subsystem_qualification PRIVATE ../../include)
target_compile_features(digitor_transition_subsystem_qualification PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_transition_subsystem_qualification PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_transition_subsystem_qualification PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
