cmake_minimum_required(VERSION 3.21)
project(digitor_engine_ffi_platform_host LANGUAGES CXX)

add_library(digitor_engine_ffi_platform_host SHARED
  src/main/cpp/platform_host_jni.cpp
)

set_target_properties(digitor_engine_ffi_platform_host PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS OFF)

target_include_directories(digitor_engine_ffi_platform_host PRIVATE
  ../../../include
)

find_library(ANDROID_LIBRARY android REQUIRED)
target_link_libraries(digitor_engine_ffi_platform_host PRIVATE ${ANDROID_LIBRARY})

# DigitorEngine is built once by hook/build.dart as a code asset. This Gradle
# CMake target only owns Flutter's SurfaceProducer <-> ANativeWindow bridge.
