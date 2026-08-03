include("${CMAKE_CURRENT_LIST_DIR}/NativePlatformProviders.cmake")

# The Windows provider is repository-owned. Release builds may still override
# the source/identity, but no external path is required for the default Windows
# package.
if(WIN32 AND NOT DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE)
  set(DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE
      "${CMAKE_SOURCE_DIR}/src/platform/windows/windows_native_provider.cpp"
      CACHE FILEPATH "Platform-specific production provider source compiled into DigitorEngine" FORCE)
endif()
if(WIN32 AND NOT DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY)
  set(DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY
      "digitor-windows-native-provider-v1"
      CACHE STRING "Stable package/build identity for the production provider" FORCE)
endif()

# This file is loaded through CMAKE_PROJECT_TOP_LEVEL_INCLUDES by the native
# release presets. The engine target is declared later, so defer attachment
# until the top-level directory has finished processing.
function(digitor_attach_required_native_provider)
  if(NOT TARGET digitor_engine)
    message(FATAL_ERROR "digitor_engine target was not created before native-provider attachment")
  endif()
  digitor_configure_native_platform_provider(digitor_engine)
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL digitor_attach_required_native_provider)
