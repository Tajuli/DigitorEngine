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
    src/gpu/correction.cpp
    src/gpu/color_science.cpp
    src/gpu/video_texture.cpp
    src/gpu/color_pipeline.cpp
    src/gpu/rgb_curves.cpp
    src/gpu/primary_wheels.cpp
    src/gpu/native_primary_wheels.cpp
    src/gpu/log_wheels.cpp
    src/gpu/native_log_wheels.cpp
    src/gpu/native_rgb_curves.cpp
    src/gpu/processed_gpu_frame.cpp
    src/gpu/preview_consumer.cpp
    src/gpu/qualifier.cpp
    src/gpu/native_hsl_qualifier.cpp
    src/gpu/lut.cpp
    src/gpu/effects.cpp
    src/gpu/node_graph.cpp
    src/gpu/production_node_graph.cpp
    src/gpu/native_node_executor.cpp
    src/gpu/native_node_kernels.cpp
    src/gpu/native_node_shader_contracts.cpp
    src/gpu/native_node_pipeline_runtime.cpp
    src/gpu/native_node_pipeline_objects.cpp
    src/gpu/native_node_backend_runtime.cpp
    src/gpu/native_node_platform_factories.cpp
    src/gpu/native_node_qualification.cpp
    src/gpu/native_node_hardware_qualification.cpp
    src/gpu/native_node_hardware_harness.cpp
    src/gpu/native_node_production_signoff.cpp
    src/cpu/cpu_backend.cpp
    src/platform/platform.cpp
    src/ffi/digitor_c_api.cpp
    src/ffi/flutter_sdk.cpp
    src/ffi/native_preview_presentation.cpp
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
    target_sources(digitor_engine PRIVATE src/gpu/metal_backend.mm src/gpu/native_node_metal_factory.mm)
    set_source_files_properties(src/gpu/metal_backend.mm src/gpu/native_node_metal_factory.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
elseif(ANDROID)
    target_sources(digitor_engine PRIVATE src/gpu/gles_backend.cpp src/gpu/native_node_gles_factory.cpp)
else()
    target_sources(digitor_engine PRIVATE src/gpu/native_stub.cpp)
endif()

if(ANDROID)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_DIGITOR_SHADER_HOST_TAG windows-x86_64)
        set(_DIGITOR_GLSLC_NAME glslc.exe)
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_DIGITOR_SHADER_HOST_TAG darwin-x86_64)
        set(_DIGITOR_GLSLC_NAME glslc)
    else()
        set(_DIGITOR_SHADER_HOST_TAG linux-x86_64)
        set(_DIGITOR_GLSLC_NAME glslc)
    endif()
    set(DIGITOR_ANDROID_GLSLC
        "${ANDROID_NDK}/shader-tools/${_DIGITOR_SHADER_HOST_TAG}/${_DIGITOR_GLSLC_NAME}"
        CACHE FILEPATH "Host glslc used to build embedded Android Vulkan shaders")
    if(NOT EXISTS "${DIGITOR_ANDROID_GLSLC}")
        find_program(DIGITOR_ANDROID_GLSLC NAMES glslc REQUIRED)
    endif()

    set(_DIGITOR_SPIRV_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/spirv")
    file(MAKE_DIRECTORY "${_DIGITOR_SPIRV_DIR}")

    function(digitor_add_embedded_glsl_spirv NAME SOURCE SYMBOL)
        set(SPV "${_DIGITOR_SPIRV_DIR}/${NAME}.spv")
        set(HEADER "${_DIGITOR_SPIRV_DIR}/${NAME}.hpp")
        add_custom_command(
            OUTPUT "${SPV}"
            COMMAND "${DIGITOR_ANDROID_GLSLC}"
                    -fshader-stage=compute --target-env=vulkan1.1 -O
                    "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" -o "${SPV}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"
            VERBATIM)
        add_custom_command(
            OUTPUT "${HEADER}"
            COMMAND "${CMAKE_COMMAND}"
                    -DINPUT=${SPV} -DOUTPUT=${HEADER} -DSYMBOL=${SYMBOL}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
            DEPENDS "${SPV}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
            VERBATIM)
        set(DIGITOR_EMBEDDED_SPIRV_HEADERS ${DIGITOR_EMBEDDED_SPIRV_HEADERS} "${HEADER}" PARENT_SCOPE)
    endfunction()

    function(digitor_add_embedded_spirv NAME SOURCE SYMBOL)
        set(OPTIONS ${ARGN})
        set(SPV "${_DIGITOR_SPIRV_DIR}/${NAME}.spv")
        set(HEADER "${_DIGITOR_SPIRV_DIR}/${NAME}.hpp")
        add_custom_command(
            OUTPUT "${SPV}"
            COMMAND "${DIGITOR_ANDROID_GLSLC}"
                    -x hlsl -fshader-stage=compute -fentry-point=main
                    --target-env=vulkan1.1 -O -fhlsl-offsets
                    -DDIGITOR_VULKAN=1 ${OPTIONS}
                    "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" -o "${SPV}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"
            VERBATIM)
        add_custom_command(
            OUTPUT "${HEADER}"
            COMMAND "${CMAKE_COMMAND}"
                    -DINPUT=${SPV} -DOUTPUT=${HEADER} -DSYMBOL=${SYMBOL}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
            DEPENDS "${SPV}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
            VERBATIM)
        set(DIGITOR_EMBEDDED_SPIRV_HEADERS ${DIGITOR_EMBEDDED_SPIRV_HEADERS} "${HEADER}" PARENT_SCOPE)
    endfunction()

    digitor_add_embedded_spirv(primary_wheels_texture src/gpu/shaders/primary_wheels.hlsl digitor_primary_wheels_texture_spirv -DDIGITOR_TEXTURE_OUTPUT=1)
    digitor_add_embedded_spirv(log_wheels_texture src/gpu/shaders/log_wheels.hlsl digitor_log_wheels_texture_spirv -DDIGITOR_TEXTURE_OUTPUT=1)
    digitor_add_embedded_spirv(hsl_qualifier_texture src/gpu/shaders/hsl_qualifier.hlsl digitor_hsl_qualifier_texture_spirv -DDIGITOR_TEXTURE_OUTPUT=1)
    digitor_add_embedded_spirv(rgb_curves_texture src/gpu/shaders/rgb_curves.hlsl digitor_rgb_curves_texture_spirv -DDIGITOR_TEXTURE_OUTPUT=1)
    digitor_add_embedded_spirv(rgb_curves_buffer src/gpu/shaders/rgb_curves.hlsl digitor_rgb_curves_buffer_spirv)
    digitor_add_embedded_spirv(color_pipeline_buffer src/gpu/shaders/color_pipeline.hlsl digitor_color_pipeline_buffer_spirv)
    digitor_add_embedded_spirv(node_mixer src/gpu/shaders/node_mixer.hlsl digitor_node_mixer_spirv)
    digitor_add_embedded_spirv(node_masked_composite src/gpu/shaders/node_masked_composite.hlsl digitor_node_masked_composite_spirv)
    digitor_add_embedded_glsl_spirv(android_ahardwarebuffer_yuv_to_rgba16f src/gpu/shaders/android_ahardwarebuffer_yuv_to_rgba16f.comp digitor_android_ahardwarebuffer_yuv_to_rgba16f_spirv)

    add_custom_target(digitor_android_spirv DEPENDS ${DIGITOR_EMBEDDED_SPIRV_HEADERS})
    add_dependencies(digitor_engine digitor_android_spirv)
    target_sources(digitor_engine PRIVATE ${DIGITOR_EMBEDDED_SPIRV_HEADERS})
    target_include_directories(digitor_engine PRIVATE "${_DIGITOR_SPIRV_DIR}")
    target_compile_definitions(digitor_engine PRIVATE DIGITOR_EMBEDDED_VULKAN_SPIRV=1)
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
        tests/test_correction.cpp
        tests/test_rgb_curves.cpp
        tests/test_primary_wheels.cpp
        tests/test_log_wheels.cpp
        tests/test_hsl_qualifier.cpp
        tests/test_color_science.cpp
        tests/test_render_export.cpp
        tests/test_render_graph.cpp
        tests/test_node_system.cpp
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
    add_executable(digitor_preview_consumer_format_transition_test
        tests/test_preview_consumer_format_transition.cpp)
    set_target_properties(digitor_preview_consumer_format_transition_test PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_preview_consumer_format_transition_test PRIVATE Digitor::Engine)
    target_include_directories(digitor_preview_consumer_format_transition_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(digitor_preview_consumer_format_transition_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_preview_consumer_format_transition_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_preview_consumer_format_transition
        COMMAND digitor_preview_consumer_format_transition_test)

    set_tests_properties(digitor_native_gpu_tests PROPERTIES
        ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
        LABELS "native-gpu;hardware"
        SKIP_RETURN_CODE 77
    )

    add_executable(digitor_log_wheels_native_tests tests/test_log_wheels_native.cpp)
    set_target_properties(digitor_log_wheels_native_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_log_wheels_native_tests PRIVATE Digitor::Engine)
    target_include_directories(digitor_log_wheels_native_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(digitor_log_wheels_native_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_log_wheels_native_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_log_wheels_native_tests COMMAND digitor_log_wheels_native_tests)
    set_tests_properties(digitor_log_wheels_native_tests PROPERTIES
        ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
        LABELS "native-gpu;hardware;log-wheels;v4.9.0"
        SKIP_RETURN_CODE 77
    )

    add_executable(digitor_hsl_qualifier_native_tests tests/test_hsl_qualifier_native.cpp)
    set_target_properties(digitor_hsl_qualifier_native_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(digitor_hsl_qualifier_native_tests PRIVATE Digitor::Engine)
    target_include_directories(digitor_hsl_qualifier_native_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(digitor_hsl_qualifier_native_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_hsl_qualifier_native_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_hsl_qualifier_native_tests COMMAND digitor_hsl_qualifier_native_tests)
    set_tests_properties(digitor_hsl_qualifier_native_tests PROPERTIES
        ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
        LABELS "native-gpu;hardware;hsl-qualifier;v5.0.0"
        SKIP_RETURN_CODE 77
    )

    if(FFmpeg_FOUND)
        add_executable(digitor_media_tests tests/test_media.cpp)
        set_target_properties(digitor_media_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        target_link_libraries(digitor_media_tests PRIVATE Digitor::Engine)
        add_custom_command(TARGET digitor_media_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:digitor_media_tests>/digitor_media_fixtures"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/malformed.txt"
                "$<TARGET_FILE_DIR:digitor_media_tests>/digitor_media_fixtures/malformed.bin"
            VERBATIM
        )
        if(DIGITOR_GENERATE_TEST_MEDIA)
            add_custom_command(TARGET digitor_media_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E env
                    "FFMPEG_EXECUTABLE=${DIGITOR_FFMPEG_CLI}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_media_fixtures.sh"
                    "$<TARGET_FILE_DIR:digitor_media_tests>/digitor_media_fixtures"
                VERBATIM)
        else()
            message(STATUS "DigitorEngine media tests: optional MP4/MOV/MKV fixtures will not be generated")
        endif()
    endif()
endif()
