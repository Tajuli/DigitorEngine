cmake_minimum_required(VERSION 3.21)
project(DigitorWindowsZeroCopyProductionQualification LANGUAGES C CXX)

# Qualification modules below register tests with add_test(). Enable testing
# before including them so their CTest entries are generated reliably.
enable_testing()

set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DIGITOR_REQUIRE_FFMPEG ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. digitor-engine)
set(DIGITOR_BUILD_WINDOWS_ZERO_COPY_QUALIFICATION ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/WindowsZeroCopyQualification.cmake)
set(DIGITOR_BUILD_WINDOWS_ZERO_COPY_MEDIA_HOST ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/WindowsZeroCopyMediaHost.cmake)
set(DIGITOR_BUILD_WINDOWS_ZERO_COPY_PRODUCTION_QUALIFICATION ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/WindowsZeroCopyProductionQualification.cmake)
