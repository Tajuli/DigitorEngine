include("${CMAKE_CURRENT_LIST_DIR}/AndroidVulkanEffectShaders.cmake")

function(digitor_configure_android_vulkan_effect_pipeline target)
  if(NOT ANDROID)
    message(FATAL_ERROR "Android Vulkan effect pipeline may only be configured for Android")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Android Vulkan effect pipeline target does not exist: ${target}")
  endif()

  add_dependencies(${target} digitor_android_vulkan_effect_shaders)
  target_sources(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platform/android/android_vulkan_effect_provider.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/android/android_vulkan_builtin_effect_shaders.cpp")
  target_include_directories(${target} PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
  target_link_libraries(${target} PRIVATE vulkan)
  target_compile_definitions(${target} PRIVATE
    DIGITOR_ANDROID_VULKAN_EFFECT_PIPELINE=1)

  message(STATUS "DigitorEngine Android Vulkan built-in effect pipeline enabled")
endfunction()
