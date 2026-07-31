# Optional additive build fragment. Include explicitly from a qualification build;
# the existing DigitorEngine target is intentionally not modified by this PR.
if(NOT WIN32)
  message(FATAL_ERROR "windows_zero_copy_module.cmake requires Windows")
endif()

add_library(digitor_windows_zero_copy STATIC
  "${CMAKE_CURRENT_LIST_DIR}/../src/gpu/windows_zero_copy_import.cpp"
)
target_compile_features(digitor_windows_zero_copy PUBLIC cxx_std_20)
target_include_directories(digitor_windows_zero_copy
  PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include"
)
target_link_libraries(digitor_windows_zero_copy
  PUBLIC Digitor::Engine d3d12 dxgi
)

add_executable(digitor_windows_zero_copy_contract_tests
  "${CMAKE_CURRENT_LIST_DIR}/../tests/test_windows_zero_copy_contract.cpp"
)
target_compile_features(digitor_windows_zero_copy_contract_tests PUBLIC cxx_std_20)
target_link_libraries(digitor_windows_zero_copy_contract_tests
  PRIVATE digitor_windows_zero_copy
)
add_test(NAME digitor_windows_zero_copy_contract_tests
         COMMAND digitor_windows_zero_copy_contract_tests)
