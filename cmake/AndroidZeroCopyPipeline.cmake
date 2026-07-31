option(DIGITOR_BUILD_ANDROID_ZERO_COPY_PIPELINE "Build Android zero-copy production pipeline" OFF)

if(DIGITOR_BUILD_ANDROID_ZERO_COPY_PIPELINE)
  add_library(digitor_android_zero_copy_pipeline STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/android_zero_copy_pipeline.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/android_zero_copy_qualification.cpp)
  target_include_directories(digitor_android_zero_copy_pipeline PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_android_zero_copy_pipeline PUBLIC cxx_std_20)
  target_link_libraries(digitor_android_zero_copy_pipeline PUBLIC digitor_engine)
  if(ANDROID)
    target_link_libraries(digitor_android_zero_copy_pipeline PUBLIC android log)
  endif()

  add_executable(digitor_android_zero_copy_contract_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/android_zero_copy_contract_tests.cpp)
  target_link_libraries(digitor_android_zero_copy_contract_tests PRIVATE
    digitor_android_zero_copy_pipeline)
  add_test(NAME digitor_android_zero_copy_contract_tests
    COMMAND digitor_android_zero_copy_contract_tests)
endif()
