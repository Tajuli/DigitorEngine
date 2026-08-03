set(DIGITOR_WINDOWS_EXTERNAL_GPU_TEXTURE_HEADER "" CACHE FILEPATH
    "Header implementing the Flutter Windows external GPU texture registrar extension")
set(DIGITOR_WINDOWS_EXTERNAL_GPU_TEXTURE_LIBRARY "" CACHE FILEPATH
    "Library implementing the Flutter Windows external GPU texture registrar extension")

function(digitor_configure_windows_native_provider target)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows native provider can only be configured on Windows")
  endif()
  if(NOT EXISTS "${DIGITOR_WINDOWS_EXTERNAL_GPU_TEXTURE_HEADER}")
    message(FATAL_ERROR
      "A real external-GPU-texture Flutter Windows extension header is required. "
      "The stock pixel-buffer texture API is not accepted because it requires CPU-readable pixels.")
  endif()
  if(NOT EXISTS "${DIGITOR_WINDOWS_EXTERNAL_GPU_TEXTURE_LIBRARY}")
    message(FATAL_ERROR "Windows external GPU texture extension library is required")
  endif()

  target_sources(${target} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/windows_native_provider.cpp")
  target_link_libraries(${target} PRIVATE
    mfplat mf mfuuid dxgi d3d12
    "${DIGITOR_WINDOWS_EXTERNAL_GPU_TEXTURE_LIBRARY}")
  get_filename_component(_digitor_windows_texture_include
    "${DIGITOR_WINDOWS_EXTERNAL_GPU_TEXTURE_HEADER}" DIRECTORY)
  target_include_directories(${target} PRIVATE
    "${_digitor_windows_texture_include}")
  target_compile_definitions(${target} PRIVATE
    DIGITOR_HAS_WINDOWS_PRODUCTION_NATIVE_PROVIDER=1
    DIGITOR_WINDOWS_EXTERNAL_GPU_TEXTURE_ZERO_COPY_REQUIRED=1)
endfunction()
