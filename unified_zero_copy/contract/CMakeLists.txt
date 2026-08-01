cmake_minimum_required(VERSION 3.24)
project(DigitorUnifiedZeroCopyContract LANGUAGES C CXX)

enable_testing()
set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_UNIFIED_ZERO_COPY_RUNTIME ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_BINARY_DIR}/digitor-engine)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/UnifiedZeroCopyRuntime.cmake)
