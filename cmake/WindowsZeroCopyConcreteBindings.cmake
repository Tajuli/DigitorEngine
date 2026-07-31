option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_CONCRETE_BINDINGS "Build concrete Windows zero-copy preview and encoder bindings" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_CONCRETE_BINDINGS)
  if(NOT WIN32)
    message(FATAL_ERROR "Concrete Windows zero-copy bindings require Windows")
  endif()
  set(DIGITOR_BUILD_WINDOWS_ZERO_COPY_RUNTIME ON CACHE BOOL "" FORCE)
  set(DIGITOR_BUILD_WINDOWS_ZERO_COPY_NATIVE_CONSUMERS ON CACHE BOOL "" FORCE)
  include(${CMAKE_CURRENT_LIST_DIR}/WindowsZeroCopyNativeConsumers.cmake)
  if(NOT TARGET digitor_windows_zero_copy_native_consumers)
    message(FATAL_ERROR "Native consumer dependency target was not created")
  endif()

  add_library(digitor_windows_zero_copy_concrete_bindings STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_concrete_bindings.cpp)
  target_include_directories(digitor_windows_zero_copy_concrete_bindings PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_windows_zero_copy_concrete_bindings PUBLIC cxx_std_20)
  target_link_libraries(digitor_windows_zero_copy_concrete_bindings PUBLIC
    digitor_windows_zero_copy_native_consumers
    d3d11 d3d12 dxgi mfplat mf mfuuid mfreadwrite)

  add_executable(digitor_windows_zero_copy_concrete_bindings_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_zero_copy_concrete_bindings_tests.cpp)
  target_link_libraries(digitor_windows_zero_copy_concrete_bindings_tests PRIVATE
    digitor_windows_zero_copy_concrete_bindings)
  add_test(NAME digitor_windows_zero_copy_concrete_bindings_tests
    COMMAND digitor_windows_zero_copy_concrete_bindings_tests)
endif()
