# Canonical DigitorEngine package/runtime version.
# Every top-level package, target and generated version file must consume this value.
set(DIGITOR_ENGINE_VERSION "5.50.0")
set(DIGITOR_ENGINE_SOVERSION "5")

string(REPLACE "." ";" _digitor_version_parts "${DIGITOR_ENGINE_VERSION}")
list(GET _digitor_version_parts 0 DIGITOR_ENGINE_VERSION_MAJOR)
list(GET _digitor_version_parts 1 DIGITOR_ENGINE_VERSION_MINOR)
list(GET _digitor_version_parts 2 DIGITOR_ENGINE_VERSION_PATCH)
unset(_digitor_version_parts)

# CMakeLists creates and extends digitor_engine after this canonical version file
# is included. Defer platform image source/framework wiring until directory end.
cmake_language(DEFER CALL include
    "${CMAKE_CURRENT_LIST_DIR}/DigitorNativeImageRuntime.cmake")
