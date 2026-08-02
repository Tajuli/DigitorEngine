option(DIGITOR_REQUIRE_NATIVE_PLATFORM_PROVIDER
       "Fail configuration unless a production-native provider source is supplied for the current platform"
       OFF)

set(DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE "" CACHE FILEPATH
    "Platform-specific production provider source compiled into DigitorEngine")
set(DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY "" CACHE STRING
    "Stable package/build identity for the production provider")

function(digitor_configure_native_platform_provider target)
  if(DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE)
    if(NOT EXISTS "${DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE}")
      message(FATAL_ERROR
        "DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE does not exist: ${DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE}")
    endif()
    if(NOT DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY)
      message(FATAL_ERROR
        "DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY is required with a native provider source")
    endif()
    target_sources(${target} PRIVATE "${DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE}")
    target_compile_definitions(${target} PRIVATE
      DIGITOR_HAS_PRODUCTION_NATIVE_PROVIDER=1
      DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY="${DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY}")
    message(STATUS
      "DigitorEngine production-native provider: ${DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY}")
  elseif(DIGITOR_REQUIRE_NATIVE_PLATFORM_PROVIDER)
    message(FATAL_ERROR
      "A production release build requires DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE. "
      "Callback contracts, simulator code and compile-only shims are not accepted.")
  else()
    message(STATUS
      "DigitorEngine production-native provider: not supplied (source release gate remains closed)")
  endif()
endfunction()
