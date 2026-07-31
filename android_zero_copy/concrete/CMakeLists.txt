cmake_minimum_required(VERSION 3.20)
project(digitor_android_zero_copy_concrete_contract LANGUAGES C CXX)

enable_testing()
set(DIGITOR_BUILD_ANDROID_ZERO_COPY_PIPELINE ON CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_ANDROID_ZERO_COPY_CONCRETE_BINDINGS ON CACHE BOOL "" FORCE)
add_subdirectory(../.. digitor-engine)
include(../../cmake/AndroidZeroCopyConcreteBindings.cmake)
