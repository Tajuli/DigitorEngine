cmake_minimum_required(VERSION 3.21)

if(NOT WIN32)
  message(FATAL_ERROR "The Digitor Windows native provider can only be built on Windows")
endif()

set(DIGITOR_FLUTTER_WINDOWS_INCLUDE_DIR "" CACHE PATH
    "Directory containing flutter/plugin_registrar_windows.h and flutter/texture_registrar.h")
set(DIGITOR_FLUTTER_WINDOWS_LIBRARY "" CACHE FILEPATH
    "Flutter Windows engine import library")

if(NOT DIGITOR_FLUTTER_WINDOWS_INCLUDE_DIR)
  message(FATAL_ERROR "DIGITOR_FLUTTER_WINDOWS_INCLUDE_DIR is required")
endif()
if(NOT EXISTS "${DIGITOR_FLUTTER_WINDOWS_INCLUDE_DIR}/flutter/plugin_registrar_windows.h")
  message(FATAL_ERROR "Flutter Windows plugin registrar header was not found")
endif()
if(NOT EXISTS "${DIGITOR_FLUTTER_WINDOWS_INCLUDE_DIR}/flutter/texture_registrar.h")
  message(FATAL_ERROR "Flutter Windows texture registrar header was not found")
endif()
if(NOT DIGITOR_FLUTTER_WINDOWS_LIBRARY OR NOT EXISTS "${DIGITOR_FLUTTER_WINDOWS_LIBRARY}")
  message(FATAL_ERROR "DIGITOR_FLUTTER_WINDOWS_LIBRARY must point to the Flutter Windows import library")
endif()

add_library(digitor_windows_native_provider STATIC
  windows_native_provider.cpp
)

target_compile_features(digitor_windows_native_provider PUBLIC cxx_std_20)
target_include_directories(digitor_windows_native_provider
  PRIVATE
    "${DIGITOR_FLUTTER_WINDOWS_INCLUDE_DIR}"
    "${PROJECT_SOURCE_DIR}/include"
)

target_compile_definitions(digitor_windows_native_provider PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  DIGITOR_WINDOWS_NATIVE_PROVIDER=1
)

target_link_libraries(digitor_windows_native_provider
  PRIVATE
    "${DIGITOR_FLUTTER_WINDOWS_LIBRARY}"
    d3d12
    dxgi
    dxguid
    mf
    mfplat
    mfuuid
    mfreadwrite
    ole32
    shlwapi
)

set_target_properties(digitor_windows_native_provider PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
