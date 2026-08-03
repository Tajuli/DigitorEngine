function(digitor_enable_android_vulkan_effects_provider target)
  if(NOT ANDROID)
    message(FATAL_ERROR "Android Vulkan effects provider requires an Android toolchain")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Android Vulkan effects target does not exist: ${target}")
  endif()

  find_library(DIGITOR_ANDROID_VULKAN_LIBRARY vulkan REQUIRED)
  target_sources(${target} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../src/platform/android/android_vulkan_effect_provider.cpp")
  target_link_libraries(${target} PRIVATE ${DIGITOR_ANDROID_VULKAN_LIBRARY})
  target_compile_definitions(${target} PRIVATE
    DIGITOR_HAS_ANDROID_VULKAN_EFFECT_PROVIDER=1)
endfunction()
