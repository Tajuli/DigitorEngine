cmake_minimum_required(VERSION 3.20)
project(digitor_production_motion_blur LANGUAGES CXX)
add_executable(digitor_production_motion_blur
  main.cpp
  ../../src/gpu/production_motion_blur.cpp
  ../../src/gpu/production_motion_blur_c_api.cpp)
target_include_directories(digitor_production_motion_blur PRIVATE ../../include)
target_compile_features(digitor_production_motion_blur PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_production_motion_blur PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_motion_blur PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
