cmake_minimum_required(VERSION 3.20)
project(digitor_android_gles_effect_provider LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." digitor-engine-build)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/AndroidGlesEffectProvider.cmake")
add_library(digitor_android_gles_effect_provider_qualification STATIC
  "${CMAKE_CURRENT_LIST_DIR}/../../src/platform/android/android_gles_effect_provider.cpp")
target_include_directories(digitor_android_gles_effect_provider_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
target_link_libraries(digitor_android_gles_effect_provider_qualification PRIVATE
  digitor_engine EGL GLESv3)
