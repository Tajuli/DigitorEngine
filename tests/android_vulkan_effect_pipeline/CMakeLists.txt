cmake_minimum_required(VERSION 3.21)
project(DigitorAndroidVulkanEffectPipeline LANGUAGES C CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "This qualification target requires the Android NDK toolchain")
endif()

set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. digitor-engine-build)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/AndroidVulkanEffectShaders.cmake)

add_library(digitor_android_vulkan_effect_pipeline_qualification STATIC
    ../../src/platform/android/android_vulkan_effect_provider.cpp
    ../../src/platform/android/android_vulkan_builtin_effect_shaders.cpp)
add_dependencies(digitor_android_vulkan_effect_pipeline_qualification
    digitor_android_vulkan_effect_shaders)
target_compile_features(digitor_android_vulkan_effect_pipeline_qualification PRIVATE cxx_std_20)
target_include_directories(digitor_android_vulkan_effect_pipeline_qualification PRIVATE
    ../../include
    ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(digitor_android_vulkan_effect_pipeline_qualification
    PRIVATE Digitor::Engine vulkan)
