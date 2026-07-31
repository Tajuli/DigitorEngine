cmake_minimum_required(VERSION 3.24)
project(DigitorAndroidNativeZeroCopyContract LANGUAGES C CXX)

enable_testing()
set(DIGITOR_BUILD_ANDROID_NATIVE_ZERO_COPY ON CACHE BOOL "" FORCE)
add_subdirectory(../.. digitor-engine)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/AndroidNativeZeroCopy.cmake)
