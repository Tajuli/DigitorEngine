cmake_minimum_required(VERSION 3.21)
project(DigitorAppleMetalEffectsQualification LANGUAGES C CXX OBJCXX)

set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. digitor-engine-build)

add_executable(digitor_apple_metal_effects_qualification
    ../test_apple_metal_effects_qualification.mm
    ../../src/platform/apple/apple_metal_effect_provider.mm
    ../../src/platform/apple/apple_metal_builtin_effect_shaders.mm
)
target_compile_features(digitor_apple_metal_effects_qualification PRIVATE cxx_std_20)
target_compile_options(digitor_apple_metal_effects_qualification PRIVATE
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
)
target_link_libraries(digitor_apple_metal_effects_qualification
    PRIVATE Digitor::Engine "-framework Metal" "-framework Foundation"
)
set_target_properties(digitor_apple_metal_effects_qualification PROPERTIES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)

enable_testing()
add_test(NAME digitor_apple_metal_effects_qualification
         COMMAND digitor_apple_metal_effects_qualification)
