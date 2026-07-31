option(DIGITOR_BUILD_APPLE_ZERO_COPY_PIPELINE "Build Apple VideoToolbox Metal zero-copy pipeline" OFF)

if(DIGITOR_BUILD_APPLE_ZERO_COPY_PIPELINE)
  if(NOT APPLE)
    message(FATAL_ERROR "Apple zero-copy pipeline requires macOS or iOS")
  endif()
  enable_language(OBJCXX)
  add_library(digitor_apple_zero_copy_pipeline STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/apple_zero_copy_pipeline.mm)
  target_include_directories(digitor_apple_zero_copy_pipeline PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_apple_zero_copy_pipeline PUBLIC cxx_std_20)
  target_link_libraries(digitor_apple_zero_copy_pipeline PUBLIC digitor_engine
    "-framework CoreVideo" "-framework Metal" "-framework IOSurface"
    "-framework VideoToolbox" "-framework CoreMedia")

  add_executable(digitor_apple_zero_copy_contract_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/apple_zero_copy_contract_tests.cpp)
  target_link_libraries(digitor_apple_zero_copy_contract_tests PRIVATE
    digitor_apple_zero_copy_pipeline)
  add_test(NAME digitor_apple_zero_copy_contract_tests
    COMMAND digitor_apple_zero_copy_contract_tests)
endif()
