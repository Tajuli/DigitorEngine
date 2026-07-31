option(DIGITOR_BUILD_WINDOWS_D3D12_P010_DISPATCH "Build Windows D3D12 P010 dispatch" OFF)

if(DIGITOR_BUILD_WINDOWS_D3D12_P010_DISPATCH)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows D3D12 P010 dispatch requires Windows")
  endif()
  set(DIGITOR_BUILD_WINDOWS_D3D12_P010_CONVERTER ON CACHE BOOL "" FORCE)
  include(${CMAKE_CURRENT_LIST_DIR}/WindowsD3D12P010Converter.cmake)
  if(NOT TARGET digitor_windows_d3d12_p010_converter)
    message(FATAL_ERROR "P010 converter dependency target was not created")
  endif()
  add_library(digitor_windows_d3d12_p010_dispatch STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_d3d12_p010_dispatch.cpp)
  target_include_directories(digitor_windows_d3d12_p010_dispatch PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_windows_d3d12_p010_dispatch PUBLIC cxx_std_20)
  target_link_libraries(digitor_windows_d3d12_p010_dispatch PUBLIC
    digitor_windows_d3d12_p010_converter d3d12 d3dcompiler dxgi)

  add_executable(digitor_windows_d3d12_p010_dispatch_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_d3d12_p010_dispatch_tests.cpp)
  target_link_libraries(digitor_windows_d3d12_p010_dispatch_tests PRIVATE
    digitor_windows_d3d12_p010_dispatch)
  add_test(NAME digitor_windows_d3d12_p010_dispatch_tests
    COMMAND digitor_windows_d3d12_p010_dispatch_tests)
endif()
