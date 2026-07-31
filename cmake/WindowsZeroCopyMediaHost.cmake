option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_MEDIA_HOST "Build real-media Windows zero-copy qualification host" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_MEDIA_HOST)
  if(NOT WIN32 OR NOT DIGITOR_HAS_FFMPEG)
    message(FATAL_ERROR "Windows zero-copy media host requires Windows and FFmpeg")
  endif()

  add_library(digitor_windows_zero_copy_media_host STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_media_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_validation_io.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_rollout.cpp)
  target_include_directories(digitor_windows_zero_copy_media_host PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_link_libraries(digitor_windows_zero_copy_media_host PUBLIC
    digitor d3d11 d3d12 dxgi)
  target_compile_features(digitor_windows_zero_copy_media_host PUBLIC cxx_std_20)

  add_executable(digitor_windows_zero_copy_media
    ${CMAKE_CURRENT_LIST_DIR}/../tools/windows_zero_copy_media_cli.cpp)
  target_link_libraries(digitor_windows_zero_copy_media PRIVATE
    digitor_windows_zero_copy_media_host)
endif()
