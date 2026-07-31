option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_MEDIA_HOST "Build real-media Windows zero-copy qualification host" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_MEDIA_HOST)
  if(NOT WIN32 OR NOT FFmpeg_FOUND)
    message(FATAL_ERROR "Windows zero-copy media host requires Windows and FFmpeg")
  endif()

  add_library(digitor_windows_zero_copy_media_host STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_media_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_validation_io.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_rollout.cpp)
  target_include_directories(digitor_windows_zero_copy_media_host PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_link_libraries(digitor_windows_zero_copy_media_host PUBLIC
    Digitor::Engine d3d11 d3d12 dxgi FFmpeg::FFmpeg)
  target_compile_definitions(digitor_windows_zero_copy_media_host PRIVATE
    DIGITOR_HAS_FFMPEG=1)
  target_compile_features(digitor_windows_zero_copy_media_host PUBLIC cxx_std_20)

  add_executable(digitor_windows_zero_copy_media_cli
    ${CMAKE_CURRENT_LIST_DIR}/../tools/windows_zero_copy_media_cli.cpp)
  target_link_libraries(digitor_windows_zero_copy_media_cli PRIVATE
    digitor_windows_zero_copy_media_host d3d12 dxgi)
endif()
