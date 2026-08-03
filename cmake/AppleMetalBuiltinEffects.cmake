function(digitor_configure_apple_metal_builtin_effects target)
  if(NOT APPLE)
    message(FATAL_ERROR "Apple Metal built-in effects may only be built on Apple platforms")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Apple Metal effects target does not exist: ${target}")
  endif()

  target_sources(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platform/apple/apple_metal_effect_provider.mm"
    "${CMAKE_SOURCE_DIR}/src/platform/apple/apple_metal_builtin_effect_shaders.mm")
  target_link_libraries(${target} PRIVATE "-framework Metal" "-framework Foundation")
  target_compile_definitions(${target} PRIVATE DIGITOR_HAS_APPLE_METAL_BUILTIN_EFFECTS=1)

  message(STATUS "DigitorEngine Apple Metal built-in effects: enabled")
endfunction()
