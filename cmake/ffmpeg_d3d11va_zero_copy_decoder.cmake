# Opt-in only. Including this fragment does not modify the default engine target.
# The parent project supplies TARGET_NAME and FFmpeg include/library settings.

if(NOT WIN32)
  message(FATAL_ERROR "FFmpeg D3D11VA zero-copy decoder requires Windows")
endif()

if(NOT DEFINED TARGET_NAME)
  message(FATAL_ERROR "TARGET_NAME must name an existing target")
endif()

if(NOT TARGET ${TARGET_NAME})
  message(FATAL_ERROR "TARGET_NAME does not identify an existing target")
endif()

target_sources(${TARGET_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../src/media/ffmpeg_d3d11va_zero_copy_decoder.cpp
)

target_link_libraries(${TARGET_NAME} PRIVATE d3d11 d3d12 dxgi)
