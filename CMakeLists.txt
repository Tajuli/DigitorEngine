cmake_minimum_required(VERSION 3.21)

project(DigitorEngine
    VERSION 0.2.0
    DESCRIPTION "GPU-first cross-platform rendering engine foundation"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DIGITOR_BUILD_TESTS "Build DigitorEngine tests" ON)
option(DIGITOR_BUILD_EXAMPLES "Build DigitorEngine examples" ON)

add_library(digitor_engine
    src/core/engine.cpp
    src/core/render_context.cpp
    src/gpu/gpu_backend.cpp
    src/cpu/cpu_backend.cpp
    src/platform/platform.cpp
    src/ffi/digitor_c_api.cpp
)

add_library(Digitor::Engine ALIAS digitor_engine)

target_include_directories(digitor_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(digitor_engine PRIVATE DIGITOR_ENGINE_BUILD)
get_target_property(DIGITOR_ENGINE_TYPE digitor_engine TYPE)
if (DIGITOR_ENGINE_TYPE STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(digitor_engine PUBLIC DIGITOR_STATIC)
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
    )
    target_link_libraries(digitor_tests PRIVATE Digitor::Engine)
    target_include_directories(digitor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME digitor_tests COMMAND digitor_tests)

    add_executable(digitor_c_abi_test tests/test_c_abi.c)
    target_link_libraries(digitor_c_abi_test PRIVATE Digitor::Engine)
    add_test(NAME digitor_c_abi_test COMMAND digitor_c_abi_test)
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
