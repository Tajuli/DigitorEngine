cmake_minimum_required(VERSION 3.20)
project(digitor_spatial_compositor_qualification LANGUAGES CXX)
add_executable(digitor_spatial_compositor
  main.cpp
  ../../src/editor/spatial_compositor.cpp
  ../../src/ffi/spatial_compositor_c_api.cpp)
target_include_directories(digitor_spatial_compositor PRIVATE ../../include)
target_compile_features(digitor_spatial_compositor PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_spatial_compositor PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_spatial_compositor PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
