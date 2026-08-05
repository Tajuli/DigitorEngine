cmake_minimum_required(VERSION 3.21)
project(DigitorRealMediaPreviewExportParity LANGUAGES C CXX)

if(NOT DEFINED DIGITOR_ENGINE_ROOT)
  get_filename_component(DIGITOR_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DIGITOR_ENABLE_FFMPEG ON CACHE BOOL "" FORCE)
set(DIGITOR_REQUIRE_FFMPEG ON CACHE BOOL "" FORCE)
add_subdirectory("${DIGITOR_ENGINE_ROOT}" digitor-engine-build)

add_executable(digitor_real_media_preview_export_parity
  "${DIGITOR_ENGINE_ROOT}/tests/test_real_media_preview_export_parity.cpp")
target_compile_features(digitor_real_media_preview_export_parity PRIVATE cxx_std_20)
target_include_directories(digitor_real_media_preview_export_parity PRIVATE
  "${DIGITOR_ENGINE_ROOT}/src")
target_link_libraries(digitor_real_media_preview_export_parity PRIVATE Digitor::Engine)
