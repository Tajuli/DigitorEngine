cmake_minimum_required(VERSION 3.21)
project(DigitorNativeProviderValidation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." digitor-engine)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NativePlatformProviders.cmake")
digitor_configure_native_platform_provider(digitor_engine)

if(DIGITOR_BUILD_TESTS)
  enable_testing()

  add_executable(digitor_native_platform_provider_test
    "${CMAKE_CURRENT_LIST_DIR}/../tests/test_native_platform_provider.cpp")
  target_link_libraries(digitor_native_platform_provider_test PRIVATE Digitor::Engine)
  add_test(NAME digitor_native_platform_provider
           COMMAND digitor_native_platform_provider_test)

  add_executable(digitor_native_platform_provider_indexing_test
    "${CMAKE_CURRENT_LIST_DIR}/../tests/test_native_platform_provider_indexing.cpp")
  target_link_libraries(digitor_native_platform_provider_indexing_test PRIVATE Digitor::Engine)
  add_test(NAME digitor_native_platform_provider_indexing
           COMMAND digitor_native_platform_provider_indexing_test)

  add_executable(digitor_release_qualification_bundle_test
    "${CMAKE_CURRENT_LIST_DIR}/../tests/test_release_qualification_bundle.cpp")
  target_link_libraries(digitor_release_qualification_bundle_test PRIVATE Digitor::Engine)
  add_test(NAME digitor_release_qualification_bundle
           COMMAND digitor_release_qualification_bundle_test)
endif()
