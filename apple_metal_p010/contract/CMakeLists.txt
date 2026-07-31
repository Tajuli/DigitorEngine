cmake_minimum_required(VERSION 3.24)
project(DigitorAppleMetalP010Contract LANGUAGES C CXX OBJCXX)

enable_testing()
set(DIGITOR_BUILD_APPLE_METAL_P010 ON CACHE BOOL "" FORCE)
add_subdirectory(../.. digitor-engine)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/AppleMetalP010.cmake)
