cmake_minimum_required(VERSION 3.20)
project(DigitorAndroidPhysicalRuntime LANGUAGES CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "android_physical_runtime must be cross-compiled with the Android NDK")
endif()

add_executable(digitor_android_physical_runtime
  main.cpp
  ../../src/gpu/android_gpu_qualification.cpp)

target_include_directories(digitor_android_physical_runtime PRIVATE ../../include)
target_compile_features(digitor_android_physical_runtime PRIVATE cxx_std_20)
target_compile_options(digitor_android_physical_runtime PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers -Wno-unused-function
  -include ${CMAKE_CURRENT_SOURCE_DIR}/media_extractor_fd_compat.hpp)
target_link_libraries(digitor_android_physical_runtime PRIVATE android log mediandk vulkan)
