cmake_minimum_required(VERSION 3.21)
project(DigitorWindowsZeroCopyProduction LANGUAGES C CXX)

enable_testing()
set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_WINDOWS_ZERO_COPY_PRODUCTION ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. digitor-engine)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/WindowsZeroCopyProduction.cmake)
