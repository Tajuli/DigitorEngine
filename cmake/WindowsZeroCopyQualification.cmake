option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_QUALIFICATION "Build Windows D3D11VA/D3D12 qualification suite" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_QUALIFICATION)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows zero-copy qualification requires Windows")
  endif()
  # FFmpeg discovery happens while the engine subdirectory is configured, so
  # FFmpeg_FOUND is directory-scoped and is not a reliable signal here. Inspect
  # the engine target's concrete compile contract instead; this cannot be faked
  # by a stale -DDIGITOR_HAS_FFMPEG=ON cache entry.
  if(NOT TARGET digitor_engine)
    message(FATAL_ERROR "Windows zero-copy qualification requires digitor_engine")
  endif()
  get_target_property(_digitor_engine_compile_definitions
    digitor_engine COMPILE_DEFINITIONS)
  if(NOT _digitor_engine_compile_definitions)
    set(_digitor_engine_compile_definitions "")
  endif()
  list(FIND _digitor_engine_compile_definitions "DIGITOR_HAS_FFMPEG=1"
    _digitor_ffmpeg_definition_index)
  if(_digitor_ffmpeg_definition_index EQUAL -1)
    message(FATAL_ERROR "Windows zero-copy qualification requires FFmpeg-enabled digitor_engine")
  endif()
  unset(_digitor_engine_compile_definitions)
  unset(_digitor_ffmpeg_definition_index)

  add_library(digitor_windows_zero_copy_qualification STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_qualification.cpp)
  target_include_directories(digitor_windows_zero_copy_qualification PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_windows_zero_copy_qualification PUBLIC cxx_std_20)
  target_link_libraries(digitor_windows_zero_copy_qualification PUBLIC
    digitor_engine d3d11 d3d12 dxgi)

  add_executable(digitor_zero_copy_qualify
    ${CMAKE_CURRENT_LIST_DIR}/../tools/windows_zero_copy_qualify.cpp)
  target_link_libraries(digitor_zero_copy_qualify PRIVATE
    digitor_windows_zero_copy_qualification)

  add_executable(digitor_zero_copy_qualification_contract
    ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_zero_copy_qualification_contract.cpp)
  target_link_libraries(digitor_zero_copy_qualification_contract PRIVATE
    digitor_windows_zero_copy_qualification)
  add_test(NAME windows_zero_copy_qualification_contract
           COMMAND digitor_zero_copy_qualification_contract)

  # Exercise the exact production D3D11VA -> NT handle -> D3D12 planar-SRV
  # contract. CreateShaderResourceView returns void, so the test explicitly
  # checks GetDeviceRemovedReason after both Y and UV view creation; this keeps
  # a driver-level device-removal regression from being hidden by a green
  # compile-only qualification.
  add_executable(digitor_ffmpeg_d3d11va_surface_contract
    ${CMAKE_CURRENT_LIST_DIR}/../tests/test_ffmpeg_d3d11va_surface_contract.cpp)
  target_include_directories(digitor_ffmpeg_d3d11va_surface_contract PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src)
  target_compile_features(digitor_ffmpeg_d3d11va_surface_contract PRIVATE cxx_std_20)
  target_link_libraries(digitor_ffmpeg_d3d11va_surface_contract PRIVATE
    digitor_engine d3d11 d3d12 dxgi)
  if(MSVC)
    target_compile_options(digitor_ffmpeg_d3d11va_surface_contract PRIVATE /UNDEBUG)
  else()
    target_compile_options(digitor_ffmpeg_d3d11va_surface_contract PRIVATE -UNDEBUG)
  endif()
  add_test(NAME windows_ffmpeg_d3d11va_surface_contract
           COMMAND digitor_ffmpeg_d3d11va_surface_contract)
endif()
