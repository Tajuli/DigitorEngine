function(digitor_configure_windows_vulkan_effects target)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows Vulkan effects may only be configured on Windows")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Windows Vulkan effects target does not exist: ${target}")
  endif()

  find_package(Vulkan REQUIRED)
  target_sources(${target} PRIVATE
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform/windows/windows_vulkan_effect_provider.cpp")
  target_link_libraries(${target} PRIVATE Vulkan::Vulkan)
  target_compile_definitions(${target} PRIVATE DIGITOR_WINDOWS_VULKAN_EFFECTS=1)
endfunction()
