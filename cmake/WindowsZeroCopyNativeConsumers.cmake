option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_NATIVE_CONSUMERS "Build Windows zero-copy native consumers" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_NATIVE_CONSUMERS)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows zero-copy native consumers require Windows")
  endif()

  include(${CMAKE_CURRENT_LIST_DIR}/WindowsZeroCopyRuntime.cmake)

  add_library(digitor_windows_zero_copy_native_consumers STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_native_consumers.cpp)
  target_include_directories(digitor_windows_zero_copy_native_consumers PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_link_libraries(digitor_windows_zero_copy_native_consumers PUBLIC
    digitor_windows_zero_copy_runtime d3d12 dxgi)
  target_compile_features(digitor_windows_zero_copy_native_consumers PUBLIC cxx_std_20)

  add_executable(digitor_windows_zero_copy_native_consumers_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_zero_copy_native_consumers_tests.cpp)
  target_link_libraries(digitor_windows_zero_copy_native_consumers_tests PRIVATE
    digitor_windows_zero_copy_native_consumers)
  add_test(NAME digitor_windows_zero_copy_native_consumers_tests
    COMMAND digitor_windows_zero_copy_native_consumers_tests)
endif()
