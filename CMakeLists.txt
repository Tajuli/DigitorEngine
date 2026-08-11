cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/DigitorEngineVersion.cmake")
project(DigitorEngine
    VERSION ${DIGITOR_ENGINE_VERSION}
    DESCRIPTION "GPU-first cross-platform rendering engine foundation"
    LANGUAGES C CXX
)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/DigitorEngineBase.cmake")
set(PROJECT_VERSION "${DIGITOR_ENGINE_VERSION}")
set(PROJECT_VERSION_MAJOR "${DIGITOR_ENGINE_VERSION_MAJOR}")
set(PROJECT_VERSION_MINOR "${DIGITOR_ENGINE_VERSION_MINOR}")
set(PROJECT_VERSION_PATCH "${DIGITOR_ENGINE_VERSION_PATCH}")
set(CMAKE_PROJECT_VERSION "${DIGITOR_ENGINE_VERSION}")
set(CMAKE_PROJECT_VERSION_MAJOR "${DIGITOR_ENGINE_VERSION_MAJOR}")
set(CMAKE_PROJECT_VERSION_MINOR "${DIGITOR_ENGINE_VERSION_MINOR}")
set(CMAKE_PROJECT_VERSION_PATCH "${DIGITOR_ENGINE_VERSION_PATCH}")

find_package(Threads REQUIRED)
target_link_libraries(digitor_engine PRIVATE Threads::Threads)

option(DIGITOR_ENABLE_OCIO "Enable OpenColorIO 2.x integration when available" ON)
option(DIGITOR_REQUIRE_OCIO "Fail configuration when OpenColorIO 2.x is unavailable" OFF)
set(DIGITOR_BUILT_WITH_OCIO OFF)
if(DIGITOR_ENABLE_OCIO)
    find_package(OpenColorIO 2 QUIET CONFIG)
    if(OpenColorIO_FOUND)
        set(DIGITOR_BUILT_WITH_OCIO ON)
        target_compile_definitions(digitor_engine PRIVATE DIGITOR_HAS_OCIO=1)
        target_link_libraries(digitor_engine PUBLIC OpenColorIO::OpenColorIO)
        message(STATUS "DigitorEngine OpenColorIO: ${OpenColorIO_VERSION}")
    elseif(DIGITOR_REQUIRE_OCIO)
        message(FATAL_ERROR "DIGITOR_REQUIRE_OCIO=ON but OpenColorIO 2.x was not found")
    else()
        message(STATUS "DigitorEngine OpenColorIO: unavailable (runtime reports explicit unavailability)")
    endif()
else()
    message(STATUS "DigitorEngine OpenColorIO: disabled")
endif()

target_sources(digitor_engine PRIVATE
    src/core/production_integration_runtime.cpp
    src/core/engine_production_runtime.cpp
    src/platform/windows/windows_engine_production.cpp
    src/platform/android/android_engine_production.cpp
    src/platform/apple/apple_engine_production.cpp
    src/runtime/async_operation.cpp
    src/runtime/sdk_media_worker.cpp
    src/runtime/production_playback_engine.cpp
    src/runtime/unified_real_media_runtime.cpp
    src/runtime/production_media_graph_runtime.cpp
    src/ffi/sdk_worker_c_api.cpp
    src/ffi/production_media_c_api.cpp
    src/ffi/flutter_production_host_adapter.cpp
    src/ffi/flutter_production_plugin_bootstrap.cpp
    src/audio/audio_latency_probe.cpp
    src/audio/audio_sync_compensation.cpp
    src/audio/professional_audio_engine.cpp
    src/sdk/timeline_audio_session.cpp
    src/ffi/audio_sync_c_api.cpp
    src/audio/live_playback_sync.cpp
    src/ffi/live_playback_sync_c_api.cpp
    src/audio/playback_transport.cpp
    src/editor/multitrack_timeline.cpp
    src/editor/professional_timeline_suite.cpp
    src/editor/timeline_editing_suite.cpp
    src/editor/timeline_completion.cpp
    src/editor/timeline_track_removal.cpp
    src/editor/timeline_track_enablement.cpp
    src/ffi/timeline_completion_c_api.cpp
    src/render/production_export.cpp
    src/ffi/production_export_c_api.cpp
    src/render/ffmpeg_export_runtime.cpp
    src/render/hardware_export_runtime.cpp
    src/render/production_hardware_encode.cpp
    src/ffi/export_job_c_api.cpp
    src/render/resumable_segment_export.cpp
    src/render/timeline_render_execution.cpp
    src/render/timeline_render_runtime.cpp
    src/render/timeline_media_adapter.cpp
    src/media/production_hardware_decode.cpp
    src/media/native_surface_import.cpp
    src/media/image_io.cpp
    src/media/native_still_image_host.cpp
    src/gpu/windows_zero_copy_import.cpp
    src/media/professional_import.cpp
    src/media/camera_raw.cpp
    src/gpu/professional_color_management.cpp
    src/gpu/ocio_color_pipeline.cpp
    src/gpu/icc_display_management.cpp
    src/gpu/hdr_ecosystem.cpp
    src/gpu/professional_lut_scopes.cpp
    src/gpu/broadcast_color_matching.cpp
)

# Production hardware decode/import implementations must be part of the same
# shared/static engine artifact consumed by Flutter. Qualification-only targets
# are not sufficient for runtime provider assembly.
if(WIN32)
    target_sources(digitor_engine PRIVATE
        src/gpu/windows_d3d12_yuv_converter.cpp
        src/media/ffmpeg_d3d11va_surface.cpp)
    target_link_libraries(digitor_engine PRIVATE d3d11 dxgi)
    target_compile_definitions(digitor_engine PRIVATE DIGITOR_WINDOWS_D3D12_ZERO_COPY=1)
elseif(ANDROID)
    target_sources(digitor_engine PRIVATE
        src/media/android_mediacodec_decoder.cpp
        src/media/android_zero_copy_pipeline.cpp
        src/media/android_zero_copy_qualification.cpp
        src/media/android_native_zero_copy.cpp
        src/media/android_zero_copy_concrete_bindings.cpp)
    find_library(DIGITOR_ANDROID_MEDIANDK_LIBRARY mediandk REQUIRED)
    find_library(DIGITOR_ANDROID_VULKAN_LIBRARY vulkan REQUIRED)
    target_link_libraries(digitor_engine PRIVATE
        ${DIGITOR_ANDROID_MEDIANDK_LIBRARY}
        ${DIGITOR_ANDROID_VULKAN_LIBRARY})
elseif(APPLE)
    target_sources(digitor_engine PRIVATE
        src/media/apple_native_zero_copy.mm
        src/media/apple_zero_copy_pipeline.mm
        src/media/apple_metal_zero_copy_runtime.mm)
    set_source_files_properties(
        src/media/apple_native_zero_copy.mm
        src/media/apple_zero_copy_pipeline.mm
        src/media/apple_metal_zero_copy_runtime.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

if(WIN32)
    target_link_libraries(digitor_engine PRIVATE ole32)
elseif(APPLE)
    find_library(DIGITOR_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(DIGITOR_AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(DIGITOR_COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(DIGITOR_COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(DIGITOR_VIDEOTOOLBOX_FRAMEWORK VideoToolbox REQUIRED)
    find_library(DIGITOR_IOSURFACE_FRAMEWORK IOSurface REQUIRED)
    target_link_libraries(digitor_engine
        PRIVATE
            ${DIGITOR_COREAUDIO_FRAMEWORK}
            ${DIGITOR_AUDIOTOOLBOX_FRAMEWORK}
        PUBLIC
            ${DIGITOR_COREMEDIA_FRAMEWORK}
            ${DIGITOR_COREVIDEO_FRAMEWORK}
            ${DIGITOR_VIDEOTOOLBOX_FRAMEWORK}
            ${DIGITOR_IOSURFACE_FRAMEWORK}
    )
endif()

if(DIGITOR_BUILD_TESTS)
    add_executable(digitor_audio_latency_probe_test tests/test_audio_latency_probe.cpp)
    add_executable(digitor_audio_sync_compensation_test tests/test_audio_sync_compensation.cpp)
    add_executable(digitor_professional_audio_engine_test tests/test_professional_audio_engine.cpp)
    add_executable(digitor_live_playback_sync_test tests/test_live_playback_sync.cpp)
    add_executable(digitor_playback_transport_test tests/test_playback_transport.cpp)
    add_executable(digitor_production_playback_engine_test tests/test_production_playback_engine.cpp)
    add_executable(digitor_unified_real_media_runtime_test tests/test_unified_real_media_runtime.cpp)
    add_executable(digitor_production_media_graph_runtime_test tests/test_production_media_graph_runtime.cpp)
    add_executable(digitor_flutter_production_host_adapter_test tests/test_flutter_production_host_adapter.cpp)
    add_executable(digitor_flutter_production_plugin_bootstrap_test tests/test_flutter_production_plugin_bootstrap.cpp)
    add_executable(digitor_production_integration_runtime_test tests/test_production_integration_runtime.cpp)
    add_executable(digitor_engine_owned_platform_production_test tests/test_engine_owned_platform_production.cpp)
    target_include_directories(digitor_engine_owned_platform_production_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_executable(digitor_production_media_c_api_test tests/test_production_media_c_api.cpp)
    add_executable(digitor_multitrack_timeline_test tests/test_multitrack_timeline.cpp)
    add_executable(digitor_professional_timeline_suite_test tests/test_professional_timeline_suite.cpp)
    add_executable(digitor_timeline_editing_suite_test tests/test_timeline_editing_suite.cpp)
    add_executable(digitor_timeline_completion_test tests/test_timeline_completion.cpp)
    add_executable(digitor_timeline_track_removal_test tests/test_timeline_track_removal.cpp)
    add_executable(digitor_timeline_track_enablement_test tests/test_timeline_track_enablement.cpp)
    add_executable(digitor_production_export_test tests/test_production_export.cpp)
    add_executable(digitor_ffmpeg_export_runtime_test tests/test_ffmpeg_export_runtime.cpp)
    add_executable(digitor_hardware_export_runtime_test tests/test_hardware_export_runtime.cpp)
    add_executable(digitor_production_hardware_encode_test tests/test_production_hardware_encode.cpp)
    add_executable(digitor_export_job_c_api_test tests/test_export_job_c_api.cpp)
    add_executable(digitor_resumable_segment_export_test tests/test_resumable_segment_export.cpp)
    add_executable(digitor_timeline_render_execution_test tests/test_timeline_render_execution.cpp)
    add_executable(digitor_timeline_render_runtime_test tests/test_timeline_render_runtime.cpp)
    add_executable(digitor_timeline_media_adapter_test tests/test_timeline_media_adapter.cpp)
    add_executable(digitor_image_io_test tests/test_image_io.cpp)
    add_executable(digitor_native_still_image_host_test tests/test_native_still_image_host.cpp)
    add_executable(digitor_native_preview_presentation_test tests/test_native_preview_presentation.cpp)
    add_executable(digitor_production_hardware_decode_test tests/test_production_hardware_decode.cpp)
    add_executable(digitor_native_surface_import_test tests/test_native_surface_import.cpp)
    add_executable(digitor_professional_import_test tests/test_professional_import.cpp)
    add_executable(digitor_professional_color_management_test tests/test_professional_color_management.cpp)
    add_executable(digitor_ocio_color_pipeline_test tests/test_ocio_color_pipeline.cpp)
    add_executable(digitor_icc_display_management_test tests/test_icc_display_management.cpp)
    add_executable(digitor_camera_raw_test tests/test_camera_raw.cpp)
    add_executable(digitor_hdr_ecosystem_test tests/test_hdr_ecosystem.cpp)
    add_executable(digitor_professional_lut_scopes_test tests/test_professional_lut_scopes.cpp)
    add_executable(digitor_broadcast_color_matching_test tests/test_broadcast_color_matching.cpp)
    foreach(target digitor_engine_owned_platform_production_test digitor_production_integration_runtime_test digitor_audio_latency_probe_test digitor_audio_sync_compensation_test digitor_professional_audio_engine_test digitor_live_playback_sync_test digitor_playback_transport_test digitor_production_playback_engine_test digitor_unified_real_media_runtime_test digitor_production_media_graph_runtime_test digitor_flutter_production_host_adapter_test digitor_flutter_production_plugin_bootstrap_test digitor_production_media_c_api_test digitor_multitrack_timeline_test digitor_professional_timeline_suite_test digitor_timeline_editing_suite_test digitor_timeline_completion_test digitor_timeline_track_removal_test digitor_timeline_track_enablement_test digitor_production_export_test digitor_ffmpeg_export_runtime_test digitor_hardware_export_runtime_test digitor_production_hardware_encode_test digitor_export_job_c_api_test digitor_resumable_segment_export_test digitor_timeline_render_execution_test digitor_timeline_render_runtime_test digitor_timeline_media_adapter_test digitor_image_io_test digitor_native_still_image_host_test digitor_native_preview_presentation_test digitor_production_hardware_decode_test digitor_native_surface_import_test digitor_professional_import_test digitor_professional_color_management_test digitor_ocio_color_pipeline_test digitor_icc_display_management_test digitor_camera_raw_test digitor_hdr_ecosystem_test digitor_professional_lut_scopes_test digitor_broadcast_color_matching_test)
        set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        target_link_libraries(${target} PRIVATE Digitor::Engine)
        # These contract executables intentionally use assert() for executable
        # qualification. Keep those checks and their setup side effects enabled
        # in Release/RelWithDebInfo builds instead of allowing NDEBUG to erase
        # lifecycle, zero-copy, and export qualification coverage.
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    add_test(NAME digitor_audio_latency_probe COMMAND digitor_audio_latency_probe_test)
    add_test(NAME digitor_audio_sync_compensation COMMAND digitor_audio_sync_compensation_test)
    add_test(NAME digitor_professional_audio_engine COMMAND digitor_professional_audio_engine_test)
    add_test(NAME digitor_live_playback_sync COMMAND digitor_live_playback_sync_test)
    add_test(NAME digitor_playback_transport COMMAND digitor_playback_transport_test)
    add_test(NAME digitor_production_playback_engine COMMAND digitor_production_playback_engine_test)
    add_test(NAME digitor_unified_real_media_runtime COMMAND digitor_unified_real_media_runtime_test)
    add_test(NAME digitor_production_media_graph_runtime COMMAND digitor_production_media_graph_runtime_test)
    add_test(NAME digitor_flutter_production_host_adapter COMMAND digitor_flutter_production_host_adapter_test)
    add_test(NAME digitor_flutter_production_plugin_bootstrap COMMAND digitor_flutter_production_plugin_bootstrap_test)
    add_test(NAME digitor_production_integration_runtime COMMAND digitor_production_integration_runtime_test)
    add_test(NAME digitor_engine_owned_platform_production COMMAND digitor_engine_owned_platform_production_test)
    add_test(NAME digitor_production_media_c_api COMMAND digitor_production_media_c_api_test)
    add_test(NAME digitor_multitrack_timeline COMMAND digitor_multitrack_timeline_test)
    add_test(NAME digitor_professional_timeline_suite COMMAND digitor_professional_timeline_suite_test)
    add_test(NAME digitor_timeline_editing_suite COMMAND digitor_timeline_editing_suite_test)
    add_test(NAME digitor_timeline_completion COMMAND digitor_timeline_completion_test)
    add_test(NAME digitor_timeline_track_removal COMMAND digitor_timeline_track_removal_test)
    add_test(NAME digitor_timeline_track_enablement COMMAND digitor_timeline_track_enablement_test)
    add_test(NAME digitor_production_export COMMAND digitor_production_export_test)
    add_test(NAME digitor_ffmpeg_export_runtime COMMAND digitor_ffmpeg_export_runtime_test)
    add_test(NAME digitor_hardware_export_runtime COMMAND digitor_hardware_export_runtime_test)
    add_test(NAME digitor_production_hardware_encode COMMAND digitor_production_hardware_encode_test)
    add_test(NAME digitor_export_job_c_api COMMAND digitor_export_job_c_api_test)
    add_test(NAME digitor_resumable_segment_export COMMAND digitor_resumable_segment_export_test)
    add_test(NAME digitor_timeline_render_execution COMMAND digitor_timeline_render_execution_test)
    add_test(NAME digitor_timeline_render_runtime COMMAND digitor_timeline_render_runtime_test)
    add_test(NAME digitor_timeline_media_adapter COMMAND digitor_timeline_media_adapter_test)
    add_test(NAME digitor_image_io COMMAND digitor_image_io_test)
    add_test(NAME digitor_native_still_image_host COMMAND digitor_native_still_image_host_test)
    add_test(NAME digitor_native_preview_presentation COMMAND digitor_native_preview_presentation_test)
    add_test(NAME digitor_production_hardware_decode COMMAND digitor_production_hardware_decode_test)
    add_test(NAME digitor_native_surface_import COMMAND digitor_native_surface_import_test)
    add_test(NAME digitor_professional_import COMMAND digitor_professional_import_test)
    add_test(NAME digitor_professional_color_management COMMAND digitor_professional_color_management_test)
    add_test(NAME digitor_ocio_color_pipeline COMMAND digitor_ocio_color_pipeline_test)
    add_test(NAME digitor_icc_display_management COMMAND digitor_icc_display_management_test)
    add_test(NAME digitor_camera_raw COMMAND digitor_camera_raw_test)
    add_test(NAME digitor_hdr_ecosystem COMMAND digitor_hdr_ecosystem_test)
    add_test(NAME digitor_professional_lut_scopes COMMAND digitor_professional_lut_scopes_test)
    add_test(NAME digitor_broadcast_color_matching COMMAND digitor_broadcast_color_matching_test)
endif()

if(DIGITOR_BUILD_TESTS AND FFmpeg_FOUND)
    add_executable(digitor_ffmpeg_real_media_test tests/test_ffmpeg_real_media.cpp)
    set_target_properties(digitor_ffmpeg_real_media_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_ffmpeg_real_media_test PRIVATE Digitor::Engine)
    if(DIGITOR_REAL_MEDIA_FIXTURE)
        add_test(NAME digitor_ffmpeg_real_media COMMAND digitor_ffmpeg_real_media_test "${DIGITOR_REAL_MEDIA_FIXTURE}")
    endif()
    add_executable(digitor_ffmpeg_vfr_parity_test tests/test_ffmpeg_vfr_parity.cpp)
    set_target_properties(digitor_ffmpeg_vfr_parity_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_ffmpeg_vfr_parity_test PRIVATE Digitor::Engine)
    if(DIGITOR_VFR_MEDIA_FIXTURE)
        add_test(NAME digitor_ffmpeg_vfr_parity COMMAND digitor_ffmpeg_vfr_parity_test "${DIGITOR_VFR_MEDIA_FIXTURE}")
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set_target_properties(digitor_engine PROPERTIES
    EXPORT_NAME Engine
    VERSION ${DIGITOR_ENGINE_VERSION}
    SOVERSION ${DIGITOR_ENGINE_SOVERSION}
)
install(TARGETS digitor_engine EXPORT DigitorEngineTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/digitor DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(cmake/DigitorEngineConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfig.cmake" INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfigVersion.cmake" VERSION ${DIGITOR_ENGINE_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT DigitorEngineTargets FILE DigitorEngineTargets.cmake NAMESPACE Digitor:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfigVersion.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine")
