include(CheckIncludeFileCXX)

set(DIGITOR_FLUTTER_WINDOWS_EPHEMERAL_DIR "" CACHE PATH
    "Flutter Windows ephemeral directory containing flutter_windows.h and wrapper headers")
set(DIGITOR_NVENC_SDK_ROOT "" CACHE PATH
    "Optional NVIDIA Video Codec SDK root; required when DIGITOR_WINDOWS_ENCODER=nvenc")
set(DIGITOR_WINDOWS_ENCODER "media_foundation" CACHE STRING
    "Windows hardware encoder provider: media_foundation, nvenc, or qsv")
set_property(CACHE DIGITOR_WINDOWS_ENCODER PROPERTY STRINGS
    media_foundation nvenc qsv)

function(digitor_configure_windows_native_provider target)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows native provider may only be configured for WIN32")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Windows native provider target does not exist: ${target}")
  endif()

  if(NOT DIGITOR_FLUTTER_WINDOWS_EPHEMERAL_DIR)
    message(FATAL_ERROR
      "DIGITOR_FLUTTER_WINDOWS_EPHEMERAL_DIR is required for the real Flutter Windows texture registrar")
  endif()
  if(NOT EXISTS "${DIGITOR_FLUTTER_WINDOWS_EPHEMERAL_DIR}/flutter_windows.h")
    message(FATAL_ERROR
      "flutter_windows.h was not found under DIGITOR_FLUTTER_WINDOWS_EPHEMERAL_DIR")
  endif()

  check_include_file_cxx(d3d12.h DIGITOR_HAVE_D3D12_H)
  check_include_file_cxx(dxgi1_6.h DIGITOR_HAVE_DXGI16_H)
  check_include_file_cxx(mfapi.h DIGITOR_HAVE_MFAPI_H)
  check_include_file_cxx(mfidl.h DIGITOR_HAVE_MFIDL_H)
  check_include_file_cxx(vulkan/vulkan.h DIGITOR_HAVE_VULKAN_H)
  if(NOT DIGITOR_HAVE_D3D12_H OR NOT DIGITOR_HAVE_DXGI16_H OR
     NOT DIGITOR_HAVE_MFAPI_H OR NOT DIGITOR_HAVE_MFIDL_H OR
     NOT DIGITOR_HAVE_VULKAN_H)
    message(FATAL_ERROR
      "Windows provider requires D3D12, DXGI 1.6, Media Foundation and Vulkan SDK headers")
  endif()

  if(DIGITOR_WINDOWS_ENCODER STREQUAL "nvenc")
    if(NOT DIGITOR_NVENC_SDK_ROOT OR
       NOT EXISTS "${DIGITOR_NVENC_SDK_ROOT}/Interface/nvEncodeAPI.h")
      message(FATAL_ERROR
        "NVENC provider requires DIGITOR_NVENC_SDK_ROOT with Interface/nvEncodeAPI.h")
    endif()
    target_include_directories(${target} PRIVATE
      "${DIGITOR_NVENC_SDK_ROOT}/Interface")
    target_compile_definitions(${target} PRIVATE DIGITOR_WINDOWS_USE_NVENC=1)
  elseif(DIGITOR_WINDOWS_ENCODER STREQUAL "qsv")
    find_path(DIGITOR_ONEVPL_INCLUDE_DIR vpl/mfxvideo.h)
    find_library(DIGITOR_ONEVPL_LIBRARY NAMES vpl libvpl)
    if(NOT DIGITOR_ONEVPL_INCLUDE_DIR OR NOT DIGITOR_ONEVPL_LIBRARY)
      message(FATAL_ERROR "QSV provider requires oneVPL headers and library")
    endif()
    target_include_directories(${target} PRIVATE "${DIGITOR_ONEVPL_INCLUDE_DIR}")
    target_link_libraries(${target} PRIVATE "${DIGITOR_ONEVPL_LIBRARY}")
    target_compile_definitions(${target} PRIVATE DIGITOR_WINDOWS_USE_QSV=1)
  elseif(DIGITOR_WINDOWS_ENCODER STREQUAL "media_foundation")
    target_compile_definitions(${target} PRIVATE DIGITOR_WINDOWS_USE_MEDIA_FOUNDATION=1)
  else()
    message(FATAL_ERROR "Unsupported DIGITOR_WINDOWS_ENCODER=${DIGITOR_WINDOWS_ENCODER}")
  endif()

  target_include_directories(${target} PRIVATE
    "${DIGITOR_FLUTTER_WINDOWS_EPHEMERAL_DIR}")
  target_link_libraries(${target} PRIVATE
    d3d12 dxgi dxguid mf mfplat mfuuid shlwapi ole32 runtimeobject)
  target_compile_definitions(${target} PRIVATE
    DIGITOR_WINDOWS_PRODUCTION_NATIVE_PROVIDER=1
    WIN32_LEAN_AND_MEAN NOMINMAX)
endfunction()
