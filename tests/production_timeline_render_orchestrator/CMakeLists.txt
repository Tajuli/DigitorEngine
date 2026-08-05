cmake_minimum_required(VERSION 3.20)
project(DigitorProductionTimelineRenderOrchestrator LANGUAGES CXX)

add_executable(digitor_production_timeline_render_orchestrator
  main.cpp
  ../../src/timeline/production_timeline_render_orchestrator.cpp)

target_include_directories(digitor_production_timeline_render_orchestrator
  PRIVATE ../../include)
target_compile_features(digitor_production_timeline_render_orchestrator
  PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_timeline_render_orchestrator
    PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_timeline_render_orchestrator
    PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()