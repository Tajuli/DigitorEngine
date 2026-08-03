function(digitor_configure_apple_metal_effects_provider target)
  if(NOT APPLE)
    message(FATAL_ERROR "The Apple Metal effects provider may only be built on Apple platforms")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Apple Metal effects provider target does not exist: ${target}")
  endif()

  target_sources(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platform/apple/apple_metal_effect_provider.mm")
  target_link_libraries(${target} PRIVATE "-framework Metal" "-framework Foundation")
  target_compile_definitions(${target} PRIVATE DIGITOR_APPLE_METAL_EFFECTS_PROVIDER=1)

  message(STATUS "DigitorEngine Apple Metal effects provider: enabled")
endfunction()
