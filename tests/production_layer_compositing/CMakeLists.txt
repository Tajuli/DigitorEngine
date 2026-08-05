cmake_minimum_required(VERSION 3.20)
project(DigitorProductionLayerCompositing LANGUAGES CXX)

add_executable(digitor_production_layer_compositing
  main.cpp
  ../../src/gpu/production_layer_compositing.cpp
)

target_include_directories(digitor_production_layer_compositing PRIVATE ../../include)
target_compile_features(digitor_production_layer_compositing PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_layer_compositing PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_layer_compositing PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
