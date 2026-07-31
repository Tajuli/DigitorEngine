option(DIGITOR_BUILD_WINDOWS_ZERO_COPY_QUALIFICATION "Build Windows D3D11VA/D3D12 qualification suite" OFF)

if(DIGITOR_BUILD_WINDOWS_ZERO_COPY_QUALIFICATION)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows zero-copy qualification requires Windows")
  endif()
  if(NOT DIGITOR_HAS_FFMPEG)
    message(FATAL_ERROR "Windows zero-copy qualification requires DIGITOR_HAS_FFMPEG")
  endif()

  add_library(digitor_windows_zero_copy_qualification STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/media/windows_zero_copy_qualification.cpp)
  target_include_directories(digitor_windows_zero_copy_qualification PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_compile_features(digitor_windows_zero_copy_qualification PUBLIC cxx_std_20)
  target_link_libraries(digitor_windows_zero_copy_qualification PUBLIC
    digitor_engine d3d11 d3d12 dxgi)

  add_executable(digitor_zero_copy_qualify
    ${CMAKE_CURRENT_LIST_DIR}/../tools/windows_zero_copy_qualify.cpp)
  target_link_libraries(digitor_zero_copy_qualify PRIVATE
    digitor_windows_zero_copy_qualification)

  add_executable(digitor_zero_copy_qualification_contract
    ${CMAKE_CURRENT_LIST_DIR}/../tests/windows_zero_copy_qualification_contract.cpp)
  target_link_libraries(digitor_zero_copy_qualification_contract PRIVATE
    digitor_windows_zero_copy_qualification)
  add_test(NAME windows_zero_copy_qualification_contract
           COMMAND digitor_zero_copy_qualification_contract)
endif()
