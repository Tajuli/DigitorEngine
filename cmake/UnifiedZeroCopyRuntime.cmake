option(DIGITOR_BUILD_UNIFIED_ZERO_COPY_RUNTIME "Build unified cross-platform zero-copy runtime" OFF)

if(DIGITOR_BUILD_UNIFIED_ZERO_COPY_RUNTIME)
  add_library(digitor_unified_zero_copy_runtime STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/unified_zero_copy_runtime.cpp)
  target_include_directories(digitor_unified_zero_copy_runtime PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_unified_zero_copy_runtime PUBLIC cxx_std_20)
  target_link_libraries(digitor_unified_zero_copy_runtime PUBLIC digitor_engine)

  add_executable(digitor_unified_zero_copy_runtime_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/unified_zero_copy_runtime_tests.cpp)
  target_link_libraries(digitor_unified_zero_copy_runtime_tests PRIVATE
    digitor_unified_zero_copy_runtime)
  add_test(NAME digitor_unified_zero_copy_runtime_tests
    COMMAND digitor_unified_zero_copy_runtime_tests)
endif()
