option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_PRODUCTION_QUALIFICATION
  "Build complete Windows zero-copy production qualification" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_PRODUCTION_QUALIFICATION)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows zero-copy production qualification requires Windows")
  endif()
  if(NOT FFmpeg_FOUND)
    message(FATAL_ERROR "Windows zero-copy production qualification requires FFmpeg")
  endif()

  target_sources(digitor_engine PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src/gpu/windows_d3d12_qualified_converter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_reference_decoder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_complete_validation.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_production_gate.cpp)
  target_compile_definitions(digitor_engine PRIVATE
    DIGITOR_WINDOWS_ZERO_COPY_QUALIFICATION_READBACK=1)

  add_executable(digitor_windows_zero_copy_production_gate_tests
    ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_zero_copy_production_gate_tests.cpp)
  target_link_libraries(digitor_windows_zero_copy_production_gate_tests PRIVATE Digitor::Engine)
  add_test(NAME digitor_windows_zero_copy_production_gate_tests
    COMMAND digitor_windows_zero_copy_production_gate_tests)
endif()
