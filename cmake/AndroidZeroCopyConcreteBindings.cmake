option(DIGITOR_BUILD_ANDROID_ZERO_COPY_CONCRETE_BINDINGS "Build Android zero-copy concrete bindings" OFF)

if(DIGITOR_BUILD_ANDROID_ZERO_COPY_CONCRETE_BINDINGS)
  set(DIGITOR_BUILD_ANDROID_ZERO_COPY_PIPELINE ON CACHE BOOL "" FORCE)
  include(${CMAKE_CURRENT_LIST_DIR}/AndroidZeroCopyPipeline.cmake)
  if(NOT TARGET digitor_android_zero_copy_pipeline)
    message(FATAL_ERROR "Android zero-copy pipeline dependency target was not created")
  endif()

  add_library(digitor_android_zero_copy_concrete_bindings STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/android_zero_copy_concrete_bindings.cpp)
  target_include_directories(digitor_android_zero_copy_concrete_bindings PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_android_zero_copy_concrete_bindings PUBLIC cxx_std_20)
  target_link_libraries(digitor_android_zero_copy_concrete_bindings PUBLIC
    digitor_android_zero_copy_pipeline)

  if(ANDROID)
    target_link_libraries(digitor_android_zero_copy_concrete_bindings PUBLIC
      android mediandk EGL GLESv3 vulkan log)
  endif()

  add_executable(digitor_android_zero_copy_concrete_bindings_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/android_zero_copy_concrete_bindings_tests.cpp)
  target_link_libraries(digitor_android_zero_copy_concrete_bindings_tests PRIVATE
    digitor_android_zero_copy_concrete_bindings)
  add_test(NAME digitor_android_zero_copy_concrete_bindings_tests
    COMMAND digitor_android_zero_copy_concrete_bindings_tests)
endif()
