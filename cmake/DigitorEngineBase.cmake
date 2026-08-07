cmake_minimum_required(VERSION 3.21)

project(DigitorEngine
    VERSION 0.0.1
    DESCRIPTION "GPU-first cross-platform rendering engine foundation"
    LANGUAGES C CXX
)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/primary_wheels.hlsl" DIGITOR_PRIMARY_WHEELS_HLSL)
configure_file(cmake/primary_wheels_shader.hpp.in generated/primary_wheels_shader.hpp @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/log_wheels.hlsl" DIGITOR_LOG_WHEELS_HLSL)
configure_file(cmake/log_wheels_shader.hpp.in generated/log_wheels_shader.hpp @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/hsl_qualifier.hlsl" DIGITOR_HSL_QUALIFIER_HLSL)
configure_file(cmake/hsl_qualifier_shader.hpp.in generated/hsl_qualifier_shader.hpp @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/rgb_curves.hlsl" DIGITOR_RGB_CURVES_HLSL)
configure_file(cmake/rgb_curves_shader.hpp.in generated/rgb_curves_shader.hpp @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/color_pipeline.hlsl" DIGITOR_COLOR_PIPELINE_HLSL)
configure_file(cmake/color_pipeline_shader.hpp.in generated/color_pipeline_shader.hpp @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DIGITOR_BUILD_TESTS "Build DigitorEngine tests" ON)
option(DIGITOR_BUILD_EXAMPLES "Build DigitorEngine examples" ON)
option(DIGITOR_ENABLE_FFMPEG "Enable FFmpeg-backed media I/O when development packages are available" ON)
option(DIGITOR_REQUIRE_FFMPEG "Fail configuration when FFmpeg is unavailable" OFF)
option(DIGITOR_GENERATE_TEST_MEDIA "Generate optional container fixtures with the FFmpeg CLI" OFF)
option(DIGITOR_WARNINGS_AS_ERRORS "Treat warnings in DigitorEngine-owned code as errors" OFF)
option(DIGITOR_ENABLE_NATIVE_SHADER_COMPILER "Enable explicitly discovered native shader tools" ON)
set(DIGITOR_DXC_ROOT "" CACHE PATH "Optional DXC installation root")
set(DIGITOR_SPIRV_TOOLS_ROOT "" CACHE PATH "Optional SPIRV-Tools installation root")
if(DIGITOR_ENABLE_NATIVE_SHADER_COMPILER)
    find_program(DIGITOR_DXC_EXECUTABLE NAMES dxc HINTS "${DIGITOR_DXC_ROOT}" PATH_SUFFIXES bin)
    find_program(DIGITOR_SPIRV_VAL_EXECUTABLE NAMES spirv-val HINTS "${DIGITOR_SPIRV_TOOLS_ROOT}" PATH_SUFFIXES bin)
endif()

if(DIGITOR_REQUIRE_FFMPEG AND NOT DIGITOR_ENABLE_FFMPEG)
    message(FATAL_ERROR "DIGITOR_REQUIRE_FFMPEG=ON conflicts with DIGITOR_ENABLE_FFMPEG=OFF")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(digitor_engine
    src/core/engine.cpp
    src/core/environment.cpp
    src/core/version.cpp
    src/core/result.cpp
    src/core/render_context.cpp
    src/core/device_discovery.cpp
    src/core/renderer_info.cpp
    src/core/device_loss.cpp
    src/core/validation.cpp
    src/core/frame.cpp
    src/core/frame_pool.cpp
    src/core/frame_queue.cpp
    src/core/frame_timing.cpp
    src/core/performance.cpp
    src/core/thread_pool.cpp
    src/core/telemetry.cpp
    src/core/memory_budget.cpp
    src/core/thermal_state.cpp
    src/core/quality_scaler.cpp
    src/core/native_handle.cpp
    src/core/cancellation.cpp
    src/core/async_job.cpp
    src/cpu/cpu_backend.cpp
    src/cpu/cpu_parallel_executor.cpp
    src/gpu/backend_factory.cpp
    src/gpu/gpu_backend.cpp
    src/gpu/gpu_device.cpp
    src/gpu/gpu_resource.cpp
    src/gpu/gpu_texture.cpp
    src/gpu/gpu_buffer.cpp
    src/gpu/gpu_sampler.cpp
    src/gpu/gpu_command.cpp
    src/gpu/render_graph.cpp
    src/gpu/shader_compiler.cpp
    src/gpu/shader_reflection.cpp
    src/gpu/pipeline_cache.cpp
    src/gpu/shader_package.cpp
    src/gpu/shader_runtime.cpp
    src/gpu/native_backend_runtime.cpp
    src/gpu/native_gpu_pipeline.cpp
    src/gpu/native_texture_registry.cpp
    src/gpu/native_texture_cache.cpp
    src/gpu/native_render_session.cpp
    src/gpu/native_gpu_frame.cpp
    src/gpu/native_gpu_frame_queue.cpp
    src/gpu/native_gpu_frame_pool.cpp
    src/gpu/native_gpu_memory_budget.cpp
    src/gpu/native_gpu_telemetry.cpp
    src/gpu/native_gpu_quality_scaler.cpp
    src/gpu/native_gpu_thermal_controller.cpp
    src/gpu/native_gpu_frame_scheduler.cpp
    src/gpu/native_gpu_device_loss.cpp
    src/gpu/native_gpu_presenter.cpp
    src/gpu/native_gpu_exporter.cpp
    src/gpu/native_gpu_session.cpp
    src/gpu/gpu_image_session.cpp
    src/ffi/gpu_image_session_c_api.cpp
    src/ffi/gpu_image_node_graph_binding_c_api.cpp
    src/ffi/native_preview_presentation.cpp
    src/gpu/vulkan_backend.cpp
    src/gpu/d3d12_backend.cpp
    src/gpu/metal_backend.cpp
    src/gpu/opengles_backend.cpp
    src/gpu/color_math.cpp
    src/gpu/color_space.cpp
    src/gpu/color_pipeline.cpp
    src/gpu/primary_wheels.cpp
    src/gpu/log_wheels.cpp
    src/gpu/rgb_curves.cpp
    src/gpu/hsl_qualifier.cpp
    src/gpu/lut.cpp
    src/gpu/effects.cpp
    src/gpu/masks.cpp
    src/gpu/node_graph.cpp
    src/gpu/node_runtime.cpp
    src/gpu/node_execution.cpp
    src/gpu/production_node_graph.cpp
    src/gpu/production_node_runtime.cpp
    src/gpu/production_node_execution.cpp
    src/gpu/native_node_runtime.cpp
    src/gpu/native_node_execution.cpp
    src/gpu/native_node_runtime_factory.cpp
    src/gpu/native_effect_runtime.cpp
    src/gpu/native_effect_runtime_factory.cpp
    src/gpu/native_color_runtime.cpp
    src/gpu/native_color_runtime_factory.cpp
    src/gpu/native_color_pipeline.cpp
    src/gpu/native_color_pipeline_factory.cpp
    src/gpu/native_color_pipeline_runtime.cpp
    src/gpu/native_color_pipeline_runtime_factory.cpp
    src/gpu/native_color_pipeline_session.cpp
    src/gpu/native_color_pipeline_session_factory.cpp
    src/gpu/native_color_pipeline_execution.cpp
    src/gpu/native_color_pipeline_execution_factory.cpp
    src/gpu/native_color_pipeline_qualification.cpp
    src/gpu/native_effect_runtime_qualification.cpp
    src/gpu/native_node_runtime_qualification.cpp
    src/gpu/native_texture_qualification.cpp
    src/gpu/native_render_qualification.cpp
    src/gpu/native_backend_qualification.cpp
    src/gpu/native_gpu_pipeline_qualification.cpp
    src/gpu/native_gpu_production_qualification.cpp
    src/gpu/native_gpu_release_qualification.cpp
    src/gpu/gpu_validation.cpp
    src/gpu/backend_validation.cpp
    src/gpu/backend_qualification.cpp
    src/gpu/gpu_qualification.cpp
    src/gpu/release_qualification.cpp
    src/gpu/physical_qualification.cpp
    src/gpu/production_qualification.cpp
    src/gpu/qualification_report.cpp
    src/gpu/qualification_evidence.cpp
    src/gpu/qualification_metrics.cpp
    src/gpu/qualification_digest.cpp
    src/gpu/qualification_parity.cpp
    src/gpu/qualification_stress.cpp
    src/gpu/qualification_device_loss.cpp
    src/gpu/qualification_cancellation.cpp
    src/gpu/qualification_memory.cpp
    src/gpu/qualification_thermal.cpp
    src/gpu/qualification_lifecycle.cpp
    src/gpu/qualification_media.cpp
    src/gpu/qualification_present.cpp
    src/gpu/qualification_export.cpp
    src/gpu/qualification_decode.cpp
    src/gpu/qualification_import.cpp
    src/gpu/qualification_encode.cpp
    src/gpu/qualification_color.cpp
    src/gpu/qualification_node.cpp
    src/gpu/qualification_effects.cpp
    src/gpu/qualification_timeline.cpp
    src/gpu/qualification_playback.cpp
    src/gpu/qualification_flutter.cpp
    src/gpu/qualification_platform.cpp
    src/gpu/qualification_contract.cpp
    src/gpu/qualification_policy.cpp
    src/gpu/qualification_version.cpp
    src/gpu/qualification_release.cpp
    src/ffi/digitor_c_api.cpp
    src/ffi/flutter_sdk.cpp
)

target_include_directories(digitor_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(digitor_engine PRIVATE DIGITOR_ENGINE_BUILD)

if(WIN32 AND DIGITOR_ENGINE_STATIC)
    target_compile_definitions(digitor_engine PUBLIC DIGITOR_ENGINE_STATIC)
endif()

if(MSVC)
    target_compile_options(digitor_engine PRIVATE /W4)
    if(DIGITOR_WARNINGS_AS_ERRORS)
        target_compile_options(digitor_engine PRIVATE /WX)
    endif()
else()
    target_compile_options(digitor_engine PRIVATE -Wall -Wextra -Wpedantic)
    if(DIGITOR_WARNINGS_AS_ERRORS)
        target_compile_options(digitor_engine PRIVATE -Werror)
    endif()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(digitor_engine PRIVATE dl)
endif()

include(cmake/DigitorFFmpeg.cmake)
include(cmake/DigitorVulkan.cmake)
include(cmake/DigitorD3D12.cmake)
include(cmake/DigitorMetal.cmake)
include(cmake/DigitorOpenGLES.cmake)
include(cmake/DigitorShaderCompiler.cmake)
include(cmake/DigitorShaderReflection.cmake)
include(cmake/DigitorPipelineCache.cmake)
include(cmake/DigitorBackendRuntime.cmake)
include(cmake/DigitorNativeRuntime.cmake)
include(cmake/DigitorNativeImageRuntime.cmake)
include(cmake/DigitorValidation.cmake)
include(cmake/DigitorQualification.cmake)
include(cmake/DigitorMedia.cmake)
include(cmake/DigitorFlutter.cmake)
include(cmake/DigitorPlatform.cmake)
include(cmake/DigitorInstall.cmake)
