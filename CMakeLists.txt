cmake_minimum_required(VERSION 3.21)

project(DigitorEngine
    VERSION 5.41.0
    DESCRIPTION "GPU-first cross-platform rendering engine foundation"
    LANGUAGES C CXX
)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/DigitorEngineBase.cmake")
find_package(Threads REQUIRED)
target_link_libraries(digitor_engine PRIVATE Threads::Threads)

target_sources(digitor_engine PRIVATE
    src/runtime/async_operation.cpp
    src/runtime/sdk_media_worker.cpp
    src/runtime/production_playback_engine.cpp
    src/runtime/unified_real_media_runtime.cpp
    src/ffi/sdk_worker_c_api.cpp
    src/audio/audio_latency_probe.cpp
    src/audio/audio_sync_compensation.cpp
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
)

if(WIN32)
    target_link_libraries(digitor_engine PRIVATE ole32)
elseif(APPLE)
    find_library(DIGITOR_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    target_link_libraries(digitor_engine PRIVATE ${DIGITOR_COREAUDIO_FRAMEWORK})
endif()

if(DIGITOR_BUILD_TESTS)
    add_executable(digitor_audio_latency_probe_test tests/test_audio_latency_probe.cpp)
    add_executable(digitor_audio_sync_compensation_test tests/test_audio_sync_compensation.cpp)
    add_executable(digitor_live_playback_sync_test tests/test_live_playback_sync.cpp)
    add_executable(digitor_playback_transport_test tests/test_playback_transport.cpp)
    add_executable(digitor_production_playback_engine_test tests/test_production_playback_engine.cpp)
    add_executable(digitor_unified_real_media_runtime_test tests/test_unified_real_media_runtime.cpp)
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
    add_executable(digitor_production_hardware_decode_test tests/test_production_hardware_decode.cpp)
    foreach(target digitor_audio_latency_probe_test digitor_audio_sync_compensation_test digitor_live_playback_sync_test digitor_playback_transport_test digitor_production_playback_engine_test digitor_unified_real_media_runtime_test digitor_multitrack_timeline_test digitor_professional_timeline_suite_test digitor_timeline_editing_suite_test digitor_timeline_completion_test digitor_timeline_track_removal_test digitor_timeline_track_enablement_test digitor_production_export_test digitor_ffmpeg_export_runtime_test digitor_hardware_export_runtime_test digitor_production_hardware_encode_test digitor_export_job_c_api_test digitor_resumable_segment_export_test digitor_timeline_render_execution_test digitor_timeline_render_runtime_test digitor_timeline_media_adapter_test digitor_production_hardware_decode_test)
        set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        target_link_libraries(${target} PRIVATE Digitor::Engine)
    endforeach()
    add_test(NAME digitor_audio_latency_probe COMMAND digitor_audio_latency_probe_test)
    add_test(NAME digitor_audio_sync_compensation COMMAND digitor_audio_sync_compensation_test)
    add_test(NAME digitor_live_playback_sync COMMAND digitor_live_playback_sync_test)
    add_test(NAME digitor_playback_transport COMMAND digitor_playback_transport_test)
    add_test(NAME digitor_production_playback_engine COMMAND digitor_production_playback_engine_test)
    add_test(NAME digitor_unified_real_media_runtime COMMAND digitor_unified_real_media_runtime_test)
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
    add_test(NAME digitor_production_hardware_decode COMMAND digitor_production_hardware_decode_test)
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
set_target_properties(digitor_engine PROPERTIES EXPORT_NAME Engine VERSION 5.41.0 SOVERSION 5)
install(TARGETS digitor_engine EXPORT DigitorEngineTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/digitor DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(cmake/DigitorEngineConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfig.cmake" INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfigVersion.cmake" VERSION 5.41.0 COMPATIBILITY SameMajorVersion)
install(EXPORT DigitorEngineTargets FILE DigitorEngineTargets.cmake NAMESPACE Digitor:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/DigitorEngineConfigVersion.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine")
