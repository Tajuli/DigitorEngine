# Opt-in build integration for the isolated Windows D3D11VA -> D3D12 path.
# Include this file only from a qualification build; the default engine target
# remains unchanged until hardware sign-off passes.
if(NOT WIN32)
  message(FATAL_ERROR "Digitor Windows D3D12 YUV conversion requires Windows")
endif()

if(NOT TARGET digitor_engine)
  message(FATAL_ERROR "digitor_engine target must exist before including this file")
endif()

target_sources(digitor_engine PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/windows_zero_copy_import.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/windows_d3d12_yuv_converter.cpp)

target_link_libraries(digitor_engine PRIVATE d3d12 dxgi d3dcompiler)
target_compile_definitions(digitor_engine PRIVATE DIGITOR_WINDOWS_D3D12_ZERO_COPY=1)
