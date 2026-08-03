set(DIGITOR_WINDOWS_FLUTTER_BRIDGE_SOURCE "" CACHE FILEPATH
    "Windows Flutter plugin source implementing WindowsFlutterTextureBridge")
set(DIGITOR_WINDOWS_PROVIDER_IDENTITY "" CACHE STRING
    "Stable Windows native provider build identity")

function(digitor_configure_windows_native_provider target)
  if(NOT WIN32)
    message(FATAL_ERROR "The Windows native provider may only be built on Windows")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Windows provider target does not exist: ${target}")
  endif()
  if(NOT DIGITOR_WINDOWS_FLUTTER_BRIDGE_SOURCE OR
     NOT EXISTS "${DIGITOR_WINDOWS_FLUTTER_BRIDGE_SOURCE}")
    message(FATAL_ERROR
      "DIGITOR_WINDOWS_FLUTTER_BRIDGE_SOURCE must name a real Flutter Windows plugin source")
  endif()
  if(NOT DIGITOR_WINDOWS_PROVIDER_IDENTITY)
    message(FATAL_ERROR "DIGITOR_WINDOWS_PROVIDER_IDENTITY is required")
  endif()

  target_sources(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platform/windows/windows_native_provider.cpp"
    "${DIGITOR_WINDOWS_FLUTTER_BRIDGE_SOURCE}")
  target_link_libraries(${target} PRIVATE
    d3d12 dxgi dxguid
    mfplat mfreadwrite mfuuid
    ole32 shlwapi)
  target_compile_definitions(${target} PRIVATE
    DIGITOR_HAS_PRODUCTION_NATIVE_PROVIDER=1
    DIGITOR_WINDOWS_NATIVE_PROVIDER=1
    DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY="${DIGITOR_WINDOWS_PROVIDER_IDENTITY}")

  message(STATUS
    "DigitorEngine Windows native provider: ${DIGITOR_WINDOWS_PROVIDER_IDENTITY}")
endfunction()
