option(DIGITOR_BUILD_APPLE_METAL_ZERO_COPY_RUNTIME "Build Apple Metal zero-copy runtime" OFF)
if(DIGITOR_BUILD_APPLE_METAL_ZERO_COPY_RUNTIME)
  if(NOT APPLE)
    message(FATAL_ERROR "Apple Metal zero-copy runtime requires an Apple platform")
  endif()
  add_library(digitor_apple_metal_zero_copy_runtime STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/apple_metal_zero_copy_runtime.mm)
  target_include_directories(digitor_apple_metal_zero_copy_runtime PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_apple_metal_zero_copy_runtime PUBLIC cxx_std_20)
  target_link_libraries(digitor_apple_metal_zero_copy_runtime PUBLIC
    digitor_engine
    "-framework Metal"
    "-framework CoreVideo"
    "-framework VideoToolbox"
    "-framework IOSurface")
  add_executable(digitor_apple_metal_zero_copy_runtime_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/apple_metal_zero_copy_runtime_tests.cpp)
  target_link_libraries(digitor_apple_metal_zero_copy_runtime_tests PRIVATE
    digitor_apple_metal_zero_copy_runtime)
  add_test(NAME digitor_apple_metal_zero_copy_runtime_tests
    COMMAND digitor_apple_metal_zero_copy_runtime_tests)
endif()