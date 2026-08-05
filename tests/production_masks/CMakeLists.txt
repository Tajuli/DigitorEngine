cmake_minimum_required(VERSION 3.20)
project(digitor_production_masks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(digitor_production_masks
  main.cpp
  ../../src/gpu/production_masks.cpp)
target_include_directories(digitor_production_masks PRIVATE ../../include)
if(MSVC)
  target_compile_options(digitor_production_masks PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_masks PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
