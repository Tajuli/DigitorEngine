option(DIGITOR_BUILD_WINDOWS_D3D12_P010_CONVERTER "Build Windows GPU RGBA16F to P010 converter" OFF)

if(DIGITOR_BUILD_WINDOWS_D3D12_P010_CONVERTER)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows D3D12 P010 converter requires Windows")
  endif()

  set(DIGITOR_BUILD_WINDOWS_ZERO_COPY_CONCRETE_BINDINGS ON CACHE BOOL "" FORCE)
  include(${CMAKE_CURRENT_LIST_DIR}/WindowsZeroCopyConcreteBindings.cmake)
  if(NOT TARGET digitor_windows_zero_copy_concrete_bindings)
    message(FATAL_ERROR "P010 converter requires digitor_windows_zero_copy_concrete_bindings")
  endif()

  add_library(digitor_windows_d3d12_p010_converter STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_d3d12_p010_converter.cpp)
  target_include_directories(digitor_windows_d3d12_p010_converter PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_link_libraries(digitor_windows_d3d12_p010_converter PUBLIC
    digitor_windows_zero_copy_concrete_bindings d3d11 d3d12 dxgi)
  target_compile_features(digitor_windows_d3d12_p010_converter PUBLIC cxx_std_20)

  if(DIGITOR_BUILD_TESTS)
    add_executable(digitor_windows_d3d12_p010_converter_tests
      ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_d3d12_p010_converter_tests.cpp)
    target_link_libraries(digitor_windows_d3d12_p010_converter_tests PRIVATE
      digitor_windows_d3d12_p010_converter)
    add_test(NAME digitor_windows_d3d12_p010_converter_tests
      COMMAND digitor_windows_d3d12_p010_converter_tests)
  endif()
endif()
