cmake_minimum_required(VERSION 3.14)
set(PROJECT_NAME "digitor_engine_ffi")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "digitor_engine_ffi_plugin")

add_library(${PLUGIN_NAME} SHARED
  "digitor_engine_ffi_plugin.cpp"
  "digitor_engine_ffi_plugin_c_api.cpp"
)

apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES)
target_compile_definitions(${PLUGIN_NAME} PRIVATE DIGITOR_ENGINE_FFI_PLUGIN_EXPORTS)
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  d3d11
  dxgi)

# DigitorEngine itself is produced by hook/build.dart as a native code asset.
# The Windows plugin target is intentionally only the Flutter texture registrar
# host so the engine is not rebuilt with a reduced platform-specific feature set.
set(digitor_engine_ffi_bundled_libraries "" PARENT_SCOPE)