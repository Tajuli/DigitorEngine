set(DIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE "" CACHE FILEPATH
    "Apple Flutter plugin source implementing AppleFlutterTextureBridge")
set(DIGITOR_APPLE_PROVIDER_IDENTITY "" CACHE STRING
    "Stable Apple native provider build identity")

function(digitor_configure_apple_native_provider target)
  if(NOT APPLE)
    message(FATAL_ERROR "The Apple native provider may only be built on Apple platforms")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Apple provider target does not exist: ${target}")
  endif()
  if(NOT DIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE OR
     NOT EXISTS "${DIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE}")
    message(FATAL_ERROR
      "DIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE must name a real Flutter macOS/iOS plugin source")
  endif()
  if(NOT DIGITOR_APPLE_PROVIDER_IDENTITY)
    message(FATAL_ERROR "DIGITOR_APPLE_PROVIDER_IDENTITY is required")
  endif()

  target_sources(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platform/apple/apple_native_provider.cpp"
    "${DIGITOR_APPLE_FLUTTER_BRIDGE_SOURCE}")
  target_link_libraries(${target} PRIVATE
    "-framework Metal"
    "-framework VideoToolbox"
    "-framework CoreVideo"
    "-framework CoreMedia"
    "-framework IOSurface"
    "-framework Foundation")
  target_compile_definitions(${target} PRIVATE
    DIGITOR_HAS_PRODUCTION_NATIVE_PROVIDER=1
    DIGITOR_APPLE_NATIVE_PROVIDER=1
    DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY="${DIGITOR_APPLE_PROVIDER_IDENTITY}")

  message(STATUS
    "DigitorEngine Apple native provider: ${DIGITOR_APPLE_PROVIDER_IDENTITY}")
endfunction()
