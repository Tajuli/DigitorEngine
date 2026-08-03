cmake_minimum_required(VERSION 3.21)
project(DigitorWindowsD3D12EffectsQualification LANGUAGES C CXX)

set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. digitor-engine-build)

add_executable(digitor_windows_d3d12_effects_qualification
    ../test_windows_d3d12_effects_qualification.cpp
    ../../src/platform/windows/windows_d3d12_effect_provider.cpp
    ../../src/platform/windows/windows_d3d12_builtin_effect_shaders.cpp
)
target_compile_features(digitor_windows_d3d12_effects_qualification PRIVATE cxx_std_20)
target_link_libraries(digitor_windows_d3d12_effects_qualification
    PRIVATE Digitor::Engine d3d12 dxgi d3dcompiler
)

enable_testing()
add_test(NAME digitor_windows_d3d12_effects_qualification
         COMMAND digitor_windows_d3d12_effects_qualification)
