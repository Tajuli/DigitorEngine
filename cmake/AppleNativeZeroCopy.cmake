option(DIGITOR_BUILD_APPLE_NATIVE_ZERO_COPY "Build Apple native zero-copy bindings" OFF)

if(DIGITOR_BUILD_APPLE_NATIVE_ZERO_COPY)
  if(NOT APPLE)
    message(FATAL_ERROR "Apple native zero-copy bindings require an Apple platform")
  endif()

  enable_language(OBJCXX)
  add_library(digitor_apple_native_zero_copy STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/apple_native_zero_copy.mm)
  target_include_directories(digitor_apple_native_zero_copy PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_apple_native_zero_copy PUBLIC cxx_std_20)
  target_link_libraries(digitor_apple_native_zero_copy PUBLIC
    digitor_engine
    "-framework CoreVideo"
    "-framework Metal"
    "-framework VideoToolbox"
    "-framework CoreMedia"
    "-framework IOSurface")

  add_executable(digitor_apple_native_zero_copy_contract_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/apple_native_zero_copy_contract_tests.cpp)
  target_link_libraries(digitor_apple_native_zero_copy_contract_tests PRIVATE
    digitor_apple_native_zero_copy)
  add_test(NAME digitor_apple_native_zero_copy_contract_tests
    COMMAND digitor_apple_native_zero_copy_contract_tests)
endif()
