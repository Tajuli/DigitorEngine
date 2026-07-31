option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_PRODUCTION "Build evidence-gated Windows zero-copy production pipeline" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_PRODUCTION)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows zero-copy production pipeline requires Windows")
  endif()

  set(DIGITOR_BUILD_WINDOWS_D3D12_P010_DISPATCH ON CACHE BOOL "" FORCE)
  include(${CMAKE_CURRENT_LIST_DIR}/WindowsD3D12P010Dispatch.cmake)
  if(NOT TARGET digitor_windows_d3d12_p010_dispatch)
    message(FATAL_ERROR "D3D12 P010 dispatch dependency was not created")
  endif()

  add_library(digitor_windows_zero_copy_production STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_production.cpp)
  target_include_directories(digitor_windows_zero_copy_production PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_windows_zero_copy_production PUBLIC cxx_std_20)
  target_link_libraries(digitor_windows_zero_copy_production PUBLIC
    digitor_windows_d3d12_p010_dispatch
    digitor_windows_d3d12_p010_converter
    digitor_windows_zero_copy_concrete_bindings
    digitor_windows_zero_copy_native_consumers)

  add_executable(digitor_windows_zero_copy_production_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_zero_copy_production_tests.cpp)
  target_link_libraries(digitor_windows_zero_copy_production_tests PRIVATE
    digitor_windows_zero_copy_production)
  add_test(NAME digitor_windows_zero_copy_production_tests
    COMMAND digitor_windows_zero_copy_production_tests)
endif()
