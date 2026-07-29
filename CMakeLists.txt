cmake_minimum_required(VERSION 3.21)

project(DigitorEngine
    VERSION 5.0.0
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
    src/core/render_context.cpp
    src/core/resources.cpp
    src/gpu/gpu_backend.cpp
    src/gpu/commands.cpp
    src/gpu/shader.cpp
    src/gpu/render_graph.cpp
    src/gpu/color.cpp
    src/gpu/color_science.cpp
    src/gpu/video_texture.cpp
    src/gpu/color_pipeline.cpp
    src/gpu/rgb_curves.cpp
    src/gpu/primary_wheels.cpp
    src/gpu/native_primary_wheels.cpp
    src/gpu/log_wheels.cpp
    src/gpu/native_log_wheels.cpp
    src/gpu/native_hsl_qualifier.cpp
    src/gpu/native_rgb_curves.cpp
    src/gpu/processed_gpu_frame.cpp
    src/gpu/preview_consumer.cpp
    src/gpu/qualifier.cpp
    src/gpu/lut.cpp
    src/gpu/effects.cpp
    src/gpu/node_graph.cpp
    src/cpu/cpu_backend.cpp
    src/platform/platform.cpp
    src/ffi/digitor_c_api.cpp
    src/ffi/flutter_sdk.cpp
    src/media/media.cpp
    src/editor/timeline.cpp
    src/render/renderer.cpp
    src/render/render_policy.cpp
    src/render/export.cpp
    src/render/validation.cpp
)

set(DIGITOR_FFMPEG_STATUS "disabled")
if(DIGITOR_ENABLE_FFMPEG)
    find_package(FFmpeg QUIET COMPONENTS avcodec avformat avutil swscale swresample)
    if(FFmpeg_FOUND)
        set(DIGITOR_FFMPEG_LIBRARIES "${FFmpeg_LIBRARIES}" CACHE STRING
            "FFmpeg development libraries linked by DigitorEngine" FORCE)
        target_link_libraries(digitor_engine PRIVATE FFmpeg::FFmpeg)
        target_compile_definitions(digitor_engine PRIVATE DIGITOR_HAS_FFMPEG=1)
        set(DIGITOR_FFMPEG_STATUS "enabled and linked (${FFmpeg_VERSION})")
    endif()
    if(DIGITOR_REQUIRE_FFMPEG AND NOT FFmpeg_FOUND)
        message(FATAL_ERROR "FFmpeg development libraries (avformat, avcodec, avutil, swscale, swresample) are required")
    elseif(NOT FFmpeg_FOUND)
        set(DIGITOR_FFMPEG_STATUS "unavailable (decoder API reports unavailable)")
    endif()
endif()
message(STATUS "DigitorEngine FFmpeg: ${DIGITOR_FFMPEG_STATUS}")

find_program(DIGITOR_FFMPEG_CLI NAMES ffmpeg DOC "Optional FFmpeg command-line fixture generator")
if(DIGITOR_GENERATE_TEST_MEDIA AND NOT DIGITOR_FFMPEG_CLI)
    message(FATAL_ERROR
        "DIGITOR_GENERATE_TEST_MEDIA=ON requires the optional ffmpeg command-line executable")
elseif(DIGITOR_GENERATE_TEST_MEDIA)
    message(STATUS "DigitorEngine FFmpeg CLI: ${DIGITOR_FFMPEG_CLI} (test fixture generation enabled)")
elseif(DIGITOR_FFMPEG_CLI)
    message(STATUS "DigitorEngine FFmpeg CLI: ${DIGITOR_FFMPEG_CLI} (test fixture generation disabled)")
else()
    message(STATUS "DigitorEngine FFmpeg CLI: unavailable (test fixture generation disabled)")
endif()

if(WIN32)
    target_sources(digitor_engine PRIVATE src/gpu/d3d12_backend.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    target_sources(digitor_engine PRIVATE src/gpu/metal_backend.mm)
    set_source_files_properties(src/gpu/metal_backend.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
elseif(ANDROID)
    target_sources(digitor_engine PRIVATE src/gpu/gles_backend.cpp)
else()
    target_sources(digitor_engine PRIVATE src/gpu/native_stub.cpp)
endif()

if(WIN32 OR ANDROID OR UNIX)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_sources(digitor_engine PRIVATE src/gpu/vulkan_backend.cpp)
        target_link_libraries(digitor_engine PRIVATE Vulkan::Vulkan)
        target_compile_definitions(digitor_engine PRIVATE DIGITOR_HAS_VULKAN=1)
    endif()
endif()

add_library(Digitor::Engine ALIAS digitor_engine)
target_compile_features(digitor_engine PUBLIC cxx_std_20)

target_include_directories(digitor_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(digitor_engine PRIVATE DIGITOR_ENGINE_BUILD)
if(DIGITOR_DXC_EXECUTABLE)
    target_compile_definitions(digitor_engine PRIVATE DIGITOR_CONFIGURED_DXC="${DIGITOR_DXC_EXECUTABLE}")
    execute_process(COMMAND "${DIGITOR_DXC_EXECUTABLE}" --version OUTPUT_VARIABLE DIGITOR_DXC_VERSION ERROR_VARIABLE DIGITOR_DXC_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "DigitorEngine DXC: ${DIGITOR_DXC_EXECUTABLE} (${DIGITOR_DXC_VERSION})")
else()
    message(STATUS "DigitorEngine DXC: unavailable (GPU shader compilation fails explicitly)")
endif()
if(DIGITOR_SPIRV_VAL_EXECUTABLE)
    target_compile_definitions(digitor_engine PRIVATE DIGITOR_CONFIGURED_SPIRV_VAL="${DIGITOR_SPIRV_VAL_EXECUTABLE}")
    message(STATUS "DigitorEngine spirv-val: ${DIGITOR_SPIRV_VAL_EXECUTABLE}")
else()
    message(STATUS "DigitorEngine spirv-val: unavailable (Vulkan shader compilation fails explicitly)")
endif()

if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(digitor_engine PUBLIC DIGITOR_ENGINE_STATIC)
elseif (WIN32)
    set_target_properties(digitor_engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if (WIN32)
    target_link_libraries(digitor_engine PRIVATE d3d12 dxgi d3dcompiler)
elseif(APPLE)
    target_link_libraries(digitor_engine PRIVATE "-framework Metal" "-framework Foundation")
elseif(ANDROID)
    if(DEFINED ANDROID_PLATFORM AND ANDROID_PLATFORM MATCHES "android-([0-9]+)")
        if(CMAKE_MATCH_1 LESS 18)
            message(FATAL_ERROR "DigitorEngine GLES requires Android API 18 or newer")
        endif()
    endif()
    find_library(DIGITOR_ANDROID_EGL_LIBRARY EGL REQUIRED)
    find_library(DIGITOR_ANDROID_GLES3_LIBRARY GLESv3 REQUIRED)
    find_library(DIGITOR_ANDROID_LIBRARY android REQUIRED)
    find_library(DIGITOR_ANDROID_LOG_LIBRARY log REQUIRED)
    find_library(DIGITOR_ANDROID_DL_LIBRARY dl REQUIRED)
    target_link_libraries(digitor_engine PUBLIC
        ${DIGITOR_ANDROID_EGL_LIBRARY}
        ${DIGITOR_ANDROID_GLES3_LIBRARY}
        ${DIGITOR_ANDROID_LIBRARY}
        ${DIGITOR_ANDROID_LOG_LIBRARY}
        ${DIGITOR_ANDROID_DL_LIBRARY}
    )
endif()

if (MSVC)
    target_compile_options(digitor_engine PRIVATE /W4 /permissive-)
    if(DIGITOR_WARNINGS_AS_ERRORS)
        target_compile_options(digitor_engine PRIVATE /WX)
    endif()
else()
    target_compile_options(digitor_engine PRIVATE -Wall -Wextra -Wpedantic)
    if(DIGITOR_WARNINGS_AS_ERRORS)
        target_compile_options(digitor_engine PRIVATE -Werror)
    endif()
endif()

if (DIGITOR_BUILD_EXAMPLES)
    add_executable(digitor_info examples/digitor_info.cpp)
    set_target_properties(digitor_info PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_info PRIVATE Digitor::Engine)
endif()

if (DIGITOR_BUILD_TESTS)
    enable_testing()
    add_executable(digitor_tests
        tests/test_engine.cpp
        tests/test_editor.cpp
        tests/test_v2.cpp
        tests/test_color_pipeline.cpp
        tests/test_rgb_curves.cpp
        tests/test_primary_wheels.cpp
        tests/test_log_wheels.cpp
        tests/test_hsl_qualifier.cpp
        tests/test_color_science.cpp
        tests/test_render_export.cpp
        tests/test_render_graph.cpp
    )
    set_target_properties(digitor_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_tests PRIVATE Digitor::Engine)
    target_include_directories(digitor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(digitor_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_tests COMMAND digitor_tests)

    add_executable(digitor_native_gpu_tests tests/test_native_gpu.cpp)
    set_target_properties(digitor_native_gpu_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_native_gpu_tests PRIVATE Digitor::Engine)
    target_include_directories(digitor_native_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(digitor_native_gpu_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_native_gpu_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_native_gpu_tests COMMAND digitor_native_gpu_tests)
    set_tests_properties(digitor_native_gpu_tests PROPERTIES
        ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
        LABELS "native-gpu;hardware"
        SKIP_RETURN_CODE 77
    )
endif()
