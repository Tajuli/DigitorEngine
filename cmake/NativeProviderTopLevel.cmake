include("${CMAKE_CURRENT_LIST_DIR}/NativePlatformProviders.cmake")

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
