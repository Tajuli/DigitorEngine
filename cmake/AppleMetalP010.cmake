option(DIGITOR_BUILD_APPLE_METAL_P010 "Build Apple Metal P010 production path" OFF)

if(DIGITOR_BUILD_APPLE_METAL_P010)
  if(NOT APPLE)
    message(FATAL_ERROR "Apple Metal P010 requires an Apple platform")
  endif()
  enable_language(OBJCXX)
  add_library(digitor_apple_metal_p010 STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/apple_metal_p010.mm)
  target_include_directories(digitor_apple_metal_p010 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_apple_metal_p010 PUBLIC cxx_std_20)
  target_link_libraries(digitor_apple_metal_p010 PUBLIC digitor_engine
    "-framework CoreVideo" "-framework Metal" "-framework VideoToolbox")
  add_executable(digitor_apple_metal_p010_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/apple_metal_p010_tests.cpp)
  target_link_libraries(digitor_apple_metal_p010_tests PRIVATE digitor_apple_metal_p010)
  add_test(NAME digitor_apple_metal_p010_tests COMMAND digitor_apple_metal_p010_tests)
endif()
