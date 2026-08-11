#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Digitor::Engine" for configuration "Release"
set_property(TARGET Digitor::Engine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Digitor::Engine PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdigitor_engine.a"
  )

list(APPEND _cmake_import_check_targets Digitor::Engine )
list(APPEND _cmake_import_check_files_for_Digitor::Engine "${_IMPORT_PREFIX}/lib/libdigitor_engine.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
