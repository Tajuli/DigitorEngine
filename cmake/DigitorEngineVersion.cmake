# Canonical DigitorEngine package/runtime version.
# Every top-level package, target and generated version file must consume this value.
set(DIGITOR_ENGINE_VERSION "0.0.1")
set(DIGITOR_ENGINE_SOVERSION "0")

string(REPLACE "." ";" _digitor_version_parts "${DIGITOR_ENGINE_VERSION}")
list(GET _digitor_version_parts 0 DIGITOR_ENGINE_VERSION_MAJOR)
list(GET _digitor_version_parts 1 DIGITOR_ENGINE_VERSION_MINOR)
list(GET _digitor_version_parts 2 DIGITOR_ENGINE_VERSION_PATCH)
unset(_digitor_version_parts)

# CMakeLists creates and extends digitor_engine after this canonical version file
# is included. Capture the absolute module path now and defer target wiring until
# the end of the top-level directory, after the library target exists.
set(DIGITOR_NATIVE_IMAGE_RUNTIME_CMAKE
    "${CMAKE_CURRENT_LIST_DIR}/DigitorNativeImageRuntime.cmake")
cmake_language(DEFER CALL include "${DIGITOR_NATIVE_IMAGE_RUNTIME_CMAKE}")
