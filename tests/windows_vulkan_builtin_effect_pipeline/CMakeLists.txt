cmake_minimum_required(VERSION 3.20)
project(digitor_windows_vulkan_builtin_effect_pipeline_qualification LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." digitor-engine-build EXCLUDE_FROM_ALL)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/WindowsVulkanEffectShaders.cmake")

add_executable(digitor_windows_vulkan_builtin_effect_pipeline_qualification
  main.cpp)
target_include_directories(digitor_windows_vulkan_builtin_effect_pipeline_qualification PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../include")
target_link_libraries(digitor_windows_vulkan_builtin_effect_pipeline_qualification PRIVATE
  digitor_engine Vulkan::Vulkan)
digitor_configure_windows_vulkan_effect_shaders(
  digitor_windows_vulkan_builtin_effect_pipeline_qualification)
