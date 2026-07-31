cmake_minimum_required(VERSION 3.24)
project(DigitorAppleZeroCopyContract LANGUAGES C CXX OBJCXX)

enable_testing()
add_subdirectory(../.. digitor-engine)
set(DIGITOR_BUILD_APPLE_ZERO_COPY_PIPELINE ON CACHE BOOL "" FORCE)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/AppleZeroCopyPipeline.cmake)
