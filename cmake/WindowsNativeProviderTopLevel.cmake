include("${CMAKE_CURRENT_LIST_DIR}/NativeProviderTopLevel.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WindowsNativeProvider.cmake")

function(digitor_attach_windows_native_sdk_dependencies)
  if(NOT TARGET digitor_engine)
    message(FATAL_ERROR "digitor_engine target was not created before Windows SDK attachment")
  endif()
  digitor_configure_windows_native_provider(digitor_engine)
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
               CALL digitor_attach_windows_native_sdk_dependencies)
