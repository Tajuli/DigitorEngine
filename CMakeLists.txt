cmake_minimum_required(VERSION 3.21)

project(DigitorEngine
    VERSION 3.1.0
    DESCRIPTION "GPU-first cross-platform rendering engine foundation"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DIGITOR_BUILD_TESTS "Build DigitorEngine tests" ON)
option(DIGITOR_BUILD_EXAMPLES "Build DigitorEngine examples" ON)
option(DIGITOR_ENABLE_FFMPEG "Enable FFmpeg-backed media I/O when development packages are available" ON)
option(DIGITOR_REQUIRE_FFMPEG "Fail configuration when FFmpeg is unavailable" OFF)

add_library(digitor_engine
    src/core/engine.cpp
    src/core/render_context.cpp
    src/core/resources.cpp
    src/gpu/gpu_backend.cpp
    src/gpu/commands.cpp
    src/gpu/shader.cpp
    src/gpu/render_graph.cpp
    src/gpu/color.cpp
    src/gpu/lut.cpp
    src/gpu/effects.cpp
    src/gpu/node_graph.cpp
    src/cpu/cpu_backend.cpp
    src/platform/platform.cpp
    src/ffi/digitor_c_api.cpp
    src/media/media.cpp
    src/editor/timeline.cpp
    src/render/renderer.cpp
)

if(DIGITOR_ENABLE_FFMPEG)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)
        if(FFMPEG_FOUND)
            target_link_libraries(digitor_engine PRIVATE PkgConfig::FFMPEG)
            target_compile_definitions(digitor_engine PRIVATE DIGITOR_HAS_FFMPEG=1)
        endif()
    endif()
    if(DIGITOR_REQUIRE_FFMPEG AND NOT FFMPEG_FOUND)
        message(FATAL_ERROR "FFmpeg development libraries (avformat, avcodec, avutil, swscale, swresample) are required")
    elseif(NOT FFMPEG_FOUND)
        message(STATUS "FFmpeg not found: real media decoder API will report unavailable")
    else()
        message(STATUS "FFmpeg ${FFMPEG_VERSION} discovered with pkg-config")
    endif()
endif()

if(WIN32)
    target_sources(digitor_engine PRIVATE src/gpu/d3d12_backend.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    target_sources(digitor_engine PRIVATE src/gpu/metal_backend.mm)
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

target_include_directories(digitor_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(digitor_engine PRIVATE DIGITOR_ENGINE_BUILD)

# add_library() follows BUILD_SHARED_LIBS. Static Windows consumers must not see
# __declspec(dllimport), otherwise MSVC emits references to non-existent __imp_ symbols.
if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(digitor_engine PUBLIC DIGITOR_ENGINE_STATIC)
elseif (WIN32)
    # Internal C++ test seams are not part of the public C ABI, but must be exported
    # so the shared-library test executables can link on Windows.
    set_target_properties(digitor_engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if (WIN32)
    target_link_libraries(digitor_engine PRIVATE d3d12 dxgi)
elseif(APPLE)
    target_link_libraries(digitor_engine PRIVATE "-framework Metal" "-framework Foundation")
elseif(ANDROID)
    target_link_libraries(digitor_engine PRIVATE dl EGL GLESv2)
endif()

if (MSVC)
    target_compile_options(digitor_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(digitor_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (DIGITOR_BUILD_EXAMPLES)
    add_executable(digitor_info examples/digitor_info.cpp)
    target_link_libraries(digitor_info PRIVATE Digitor::Engine)
endif()

if (DIGITOR_BUILD_TESTS)
    enable_testing()
    add_executable(digitor_tests
        tests/test_engine.cpp
        tests/test_editor.cpp
        tests/test_v2.cpp
    )
    target_link_libraries(digitor_tests PRIVATE Digitor::Engine)
    target_include_directories(digitor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(digitor_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_tests COMMAND digitor_tests)

    if (WIN32)
        add_executable(digitor_native_gpu_tests tests/test_native_gpu.cpp)
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
        )
    endif()

    if(FFMPEG_FOUND)
        add_executable(digitor_media_tests tests/test_media.cpp)
        target_link_libraries(digitor_media_tests PRIVATE Digitor::Engine)
        add_custom_command(TARGET digitor_media_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:digitor_media_tests>/digitor_media_fixtures"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/malformed.txt"
                "$<TARGET_FILE_DIR:digitor_media_tests>/digitor_media_fixtures/malformed.bin"
            VERBATIM
        )
        add_test(NAME digitor_media_tests COMMAND digitor_media_tests)
        set_tests_properties(digitor_media_tests PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:digitor_media_tests>"
        )
    endif()

    add_executable(digitor_c_header_test tests/test_c_header.c)
    set_target_properties(digitor_c_header_test PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(digitor_c_header_test PRIVATE Digitor::Engine)
    if (MSVC)
        target_compile_options(digitor_c_header_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_c_header_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_c_header_test COMMAND digitor_c_header_test)
endif()

include(GNUInstallDirs)

install(TARGETS digitor_engine
    EXPORT DigitorEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT DigitorEngineTargets
    FILE DigitorEngineTargets.cmake
    NAMESPACE Digitor::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DigitorEngine
)
