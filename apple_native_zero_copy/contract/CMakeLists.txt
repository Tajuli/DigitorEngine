cmake_minimum_required(VERSION 3.24)
project(DigitorAppleNativeZeroCopyContract LANGUAGES C CXX OBJCXX)

enable_testing()
add_subdirectory(../.. digitor-engine)
set(DIGITOR_BUILD_APPLE_NATIVE_ZERO_COPY ON CACHE BOOL "" FORCE)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/AppleNativeZeroCopy.cmake)
