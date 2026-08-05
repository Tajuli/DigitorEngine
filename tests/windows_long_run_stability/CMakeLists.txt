cmake_minimum_required(VERSION 3.21)
project(DigitorWindowsLongRunStability LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Windows long-run stability qualification requires Windows")
endif()
if(NOT DEFINED DIGITOR_ENGINE_ROOT)
  message(FATAL_ERROR "DIGITOR_ENGINE_ROOT is required")
endif()

set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DIGITOR_ENABLE_FFMPEG ON CACHE BOOL "" FORCE)
set(DIGITOR_REQUIRE_FFMPEG ON CACHE BOOL "" FORCE)
set(DIGITOR_FFMPEG_ROOT "${DIGITOR_FFMPEG_ROOT}" CACHE PATH "" FORCE)
add_subdirectory("${DIGITOR_ENGINE_ROOT}" digitor-engine-build)

add_executable(digitor_windows_long_run_stability
  "${DIGITOR_ENGINE_ROOT}/tests/test_windows_long_run_stability.cpp")
target_include_directories(digitor_windows_long_run_stability PRIVATE
  "${DIGITOR_ENGINE_ROOT}/tests"
  "${DIGITOR_ENGINE_ROOT}/src")
target_link_libraries(digitor_windows_long_run_stability PRIVATE Digitor::Engine psapi)
set_target_properties(digitor_windows_long_run_stability PROPERTIES
  CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
