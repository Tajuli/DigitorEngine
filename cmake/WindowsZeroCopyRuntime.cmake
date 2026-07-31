option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_RUNTIME "Build evidence-gated Windows zero-copy production runtime" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_RUNTIME)
  add_library(digitor_windows_zero_copy_runtime STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_frame_broker.cpp)
  target_include_directories(digitor_windows_zero_copy_runtime PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_link_libraries(digitor_windows_zero_copy_runtime PUBLIC digitor_engine)
  target_compile_features(digitor_windows_zero_copy_runtime PUBLIC cxx_std_20)

  if(DIGITOR_BUILD_TESTS)
    add_executable(digitor_windows_zero_copy_runtime_tests
      ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_zero_copy_runtime_tests.cpp)
    target_link_libraries(digitor_windows_zero_copy_runtime_tests PRIVATE
      digitor_windows_zero_copy_runtime)
    add_test(NAME digitor_windows_zero_copy_runtime_tests
      COMMAND digitor_windows_zero_copy_runtime_tests)
  endif()
endif()
