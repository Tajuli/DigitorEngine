# Opt-in Windows FFmpeg D3D11VA extraction module.
# Include only from a Windows qualification build after digitor_engine and
# FFmpeg::FFmpeg are available. The default build remains unchanged.
if(WIN32 AND TARGET digitor_engine AND TARGET FFmpeg::FFmpeg)
  target_sources(digitor_engine PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/ffmpeg_d3d11va_surface.cpp)
  target_link_libraries(digitor_engine PRIVATE d3d11 dxgi)

  if(DIGITOR_BUILD_TESTS)
    add_executable(digitor_ffmpeg_d3d11va_surface_contract
      ${CMAKE_CURRENT_LIST_DIR}/../tests/test_ffmpeg_d3d11va_surface_contract.cpp)
    target_link_libraries(digitor_ffmpeg_d3d11va_surface_contract
      PRIVATE Digitor::Engine)
    add_test(NAME digitor_ffmpeg_d3d11va_surface_contract
      COMMAND digitor_ffmpeg_d3d11va_surface_contract)
  endif()
endif()
