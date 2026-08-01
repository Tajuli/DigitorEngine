include("${CMAKE_CURRENT_LIST_DIR}/cmake/DigitorEngineBase.cmake")

set(PROJECT_VERSION 5.17.0)
set(PROJECT_VERSION_MAJOR 5)
set(PROJECT_VERSION_MINOR 17)
set(PROJECT_VERSION_PATCH 0)

target_sources(digitor_engine PRIVATE
    src/runtime/async_operation.cpp
    src/runtime/sdk_media_worker.cpp
    src/ffi/sdk_worker_c_api.cpp
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set_target_properties(digitor_engine PROPERTIES
    EXPORT_NAME Engine
    VERSION 5.17.0
    SOVERSION 5
)

install(TARGETS digitor_engine
    EXPORT DigitorEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/digitor DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/DigitorEngineConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfigVersion.cmake"
    VERSION 5.17.0
    COMPATIBILITY SameMajorVersion
)
install(EXPORT DigitorEngineTargets
    FILE DigitorEngineTargets.cmake
    NAMESPACE Digitor::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine"
)
