cmake_minimum_required(VERSION 3.20)
project(digitor_production_lens_distortion LANGUAGES CXX)
add_executable(digitor_production_lens_distortion
  main.cpp
  ../../src/gpu/production_lens_distortion.cpp)
target_include_directories(digitor_production_lens_distortion PRIVATE ../../include)
target_compile_features(digitor_production_lens_distortion PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_production_lens_distortion PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_lens_distortion PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
