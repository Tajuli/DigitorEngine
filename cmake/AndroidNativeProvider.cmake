set(DIGITOR_ANDROID_FLUTTER_BRIDGE_SOURCE "" CACHE FILEPATH
    "Android Flutter plugin source implementing AndroidFlutterTextureBridge")
set(DIGITOR_ANDROID_PROVIDER_IDENTITY "" CACHE STRING
    "Stable Android native provider build identity")

function(digitor_configure_android_native_provider target)
  if(NOT ANDROID)
    message(FATAL_ERROR "The Android native provider may only be built with the Android NDK")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Android provider target does not exist: ${target}")
  endif()
  if(ANDROID_PLATFORM_LEVEL LESS 26)
    message(FATAL_ERROR "Android native provider requires API level 26 or newer")
  endif()
  if(NOT DIGITOR_ANDROID_FLUTTER_BRIDGE_SOURCE OR
     NOT EXISTS "${DIGITOR_ANDROID_FLUTTER_BRIDGE_SOURCE}")
    message(FATAL_ERROR
      "DIGITOR_ANDROID_FLUTTER_BRIDGE_SOURCE must name a real Flutter Android plugin source")
  endif()
  if(NOT DIGITOR_ANDROID_PROVIDER_IDENTITY)
    message(FATAL_ERROR "DIGITOR_ANDROID_PROVIDER_IDENTITY is required")
  endif()

  find_library(DIGITOR_ANDROID_LIBRARY android REQUIRED)
  find_library(DIGITOR_MEDIANDK_LIBRARY mediandk REQUIRED)
  find_library(DIGITOR_LOG_LIBRARY log REQUIRED)
  find_library(DIGITOR_EGL_LIBRARY EGL REQUIRED)
  find_library(DIGITOR_GLES3_LIBRARY GLESv3 REQUIRED)
  find_library(DIGITOR_VULKAN_LIBRARY vulkan REQUIRED)

  target_sources(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/platform/android/android_native_provider.cpp"
    "${DIGITOR_ANDROID_FLUTTER_BRIDGE_SOURCE}")
  target_link_libraries(${target} PRIVATE
    ${DIGITOR_ANDROID_LIBRARY}
    ${DIGITOR_MEDIANDK_LIBRARY}
    ${DIGITOR_LOG_LIBRARY}
    ${DIGITOR_EGL_LIBRARY}
    ${DIGITOR_GLES3_LIBRARY}
    ${DIGITOR_VULKAN_LIBRARY})
  target_compile_definitions(${target} PRIVATE
    DIGITOR_HAS_PRODUCTION_NATIVE_PROVIDER=1
    DIGITOR_ANDROID_NATIVE_PROVIDER=1
    DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY="${DIGITOR_ANDROID_PROVIDER_IDENTITY}")

  message(STATUS
    "DigitorEngine Android native provider: ${DIGITOR_ANDROID_PROVIDER_IDENTITY}")
endfunction()
