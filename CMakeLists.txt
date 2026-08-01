cmake_minimum_required(VERSION 3.21)

project(DigitorEngine
    VERSION 5.19.0
    DESCRIPTION "GPU-first cross-platform rendering engine foundation"
    LANGUAGES C CXX
)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/DigitorEngineBase.cmake")

target_sources(digitor_engine PRIVATE
    src/runtime/async_operation.cpp
    src/runtime/sdk_media_worker.cpp
    src/ffi/sdk_worker_c_api.cpp
)

if(DIGITOR_BUILD_TESTS AND FFmpeg_FOUND)
    add_executable(digitor_ffmpeg_real_media_test tests/test_ffmpeg_real_media.cpp)
    set_target_properties(digitor_ffmpeg_real_media_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(digitor_ffmpeg_real_media_test PRIVATE Digitor::Engine)
    if(DIGITOR_REAL_MEDIA_FIXTURE)
        add_test(
            NAME digitor_ffmpeg_real_media
            COMMAND digitor_ffmpeg_real_media_test "${DIGITOR_REAL_MEDIA_FIXTURE}"
        )
    endif()

    add_executable(digitor_ffmpeg_vfr_parity_test tests/test_ffmpeg_vfr_parity.cpp)
    set_target_properties(digitor_ffmpeg_vfr_parity_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(digitor_ffmpeg_vfr_parity_test PRIVATE Digitor::Engine)
    if(DIGITOR_VFR_MEDIA_FIXTURE)
        add_test(
            NAME digitor_ffmpeg_vfr_parity
            COMMAND digitor_ffmpeg_vfr_parity_test "${DIGITOR_VFR_MEDIA_FIXTURE}"
        )
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set_target_properties(digitor_engine PROPERTIES
    EXPORT_NAME Engine
    VERSION 5.19.0
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
    VERSION 5.19.0
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
