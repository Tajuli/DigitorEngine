cmake_minimum_required(VERSION 3.20)
project(DigitorAndroidGpuQualification LANGUAGES CXX)
add_executable(digitor_android_gpu_qualification
  main.cpp
  ../../src/gpu/android_gpu_qualification.cpp)
target_include_directories(digitor_android_gpu_qualification PRIVATE ../../include)
target_compile_features(digitor_android_gpu_qualification PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_android_gpu_qualification PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_android_gpu_qualification PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
