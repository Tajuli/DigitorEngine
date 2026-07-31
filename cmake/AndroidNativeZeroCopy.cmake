option(DIGITOR_BUILD_ANDROID_NATIVE_ZERO_COPY "Build Android native zero-copy interop" OFF)

if(DIGITOR_BUILD_ANDROID_NATIVE_ZERO_COPY)
  set(DIGITOR_BUILD_ANDROID_ZERO_COPY_PIPELINE ON CACHE BOOL "" FORCE)
  include(${CMAKE_CURRENT_LIST_DIR}/AndroidZeroCopyPipeline.cmake)
  if(NOT TARGET digitor_android_zero_copy_pipeline)
    message(FATAL_ERROR "Android zero-copy pipeline dependency target was not created")
  endif()

  add_library(digitor_android_native_zero_copy STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/android_native_zero_copy.cpp)
  target_include_directories(digitor_android_native_zero_copy PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_android_native_zero_copy PUBLIC cxx_std_20)
  target_link_libraries(digitor_android_native_zero_copy PUBLIC
    digitor_android_zero_copy_pipeline)

  if(ANDROID)
    target_link_libraries(digitor_android_native_zero_copy PUBLIC
      android log vulkan EGL GLESv3)
  endif()

  add_executable(digitor_android_native_zero_copy_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/android_native_zero_copy_tests.cpp)
  target_link_libraries(digitor_android_native_zero_copy_tests PRIVATE
    digitor_android_native_zero_copy)
  add_test(NAME digitor_android_native_zero_copy_tests
    COMMAND digitor_android_native_zero_copy_tests)
endif()
