cmake_minimum_required(VERSION 3.20)
project(DigitorAndroidPhysicalRuntime LANGUAGES CXX)

if(NOT ANDROID)
  message(FATAL_ERROR "android_physical_runtime must be cross-compiled with the Android NDK")
endif()

# Compile the existing qualification entrypoint under an internal name so the
# wrapper can deterministically release retained AImageReader objects before
# process/runtime teardown begins. The API<28 compatibility branch of main.cpp
# cannot call AMediaCodec_getName(), so its codec parameter is intentionally
# unused there; scope that warning relaxation to this one source file only.
set_source_files_properties(main.cpp PROPERTIES
  COMPILE_DEFINITIONS "main=digitor_android_physical_runtime_main"
  COMPILE_OPTIONS "-Wno-unused-parameter")

add_executable(digitor_android_physical_runtime
  qualification_entry.cpp
  main.cpp
  ../../src/gpu/android_gpu_qualification.cpp)

target_include_directories(digitor_android_physical_runtime PRIVATE ../../include)
target_compile_features(digitor_android_physical_runtime PRIVATE cxx_std_20)
target_compile_options(digitor_android_physical_runtime PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers -Wno-unused-function
  -include ${CMAKE_CURRENT_SOURCE_DIR}/media_extractor_fd_compat.hpp
  -include ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_ycbcr_compat.hpp)
target_link_libraries(digitor_android_physical_runtime PRIVATE android log mediandk vulkan)
