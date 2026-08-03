option(DIGITOR_REQUIRE_NATIVE_PLATFORM_PROVIDER
       "Fail configuration unless the production provider for the current platform is configured"
       OFF)

set(DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE "" CACHE FILEPATH
    "Optional custom production provider source compiled into DigitorEngine")
set(DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY "" CACHE STRING
    "Stable package/build identity for a custom production provider")

include("${CMAKE_CURRENT_LIST_DIR}/WindowsNativeProvider.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/AndroidNativeProvider.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/AppleNativeProvider.cmake")

function(digitor_configure_custom_native_provider target)
  if(NOT EXISTS "${DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE}")
    message(FATAL_ERROR
      "DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE does not exist: ${DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE}")
  endif()
  if(NOT DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY)
    message(FATAL_ERROR
      "DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY is required with a custom provider source")
  endif()
  target_sources(${target} PRIVATE "${DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE}")
  target_compile_definitions(${target} PRIVATE
    DIGITOR_HAS_PRODUCTION_NATIVE_PROVIDER=1
    DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY="${DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY}")
  message(STATUS
    "DigitorEngine custom production-native provider: ${DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY}")
endfunction()

function(digitor_configure_native_platform_provider target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Native provider target does not exist: ${target}")
  endif()

  if(DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE)
    digitor_configure_custom_native_provider(${target})
    return()
  endif()

  if(WIN32)
    digitor_configure_windows_native_provider(${target})
  elseif(ANDROID)
    digitor_configure_android_native_provider(${target})
  elseif(APPLE)
    digitor_configure_apple_native_provider(${target})
  elseif(DIGITOR_REQUIRE_NATIVE_PLATFORM_PROVIDER)
    message(FATAL_ERROR
      "No production-native provider package exists for the current platform")
  else()
    message(STATUS
      "DigitorEngine production-native provider: not configured for this platform")
  endif()
endfunction()
