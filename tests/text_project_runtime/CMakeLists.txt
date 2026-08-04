cmake_minimum_required(VERSION 3.20)
project(digitor_text_project_runtime LANGUAGES CXX)

add_executable(digitor_text_project_runtime
  main.cpp
  ../../src/editor/text_project_runtime.cpp
  ../../src/ffi/text_project_runtime_c_api.cpp)
target_include_directories(digitor_text_project_runtime PRIVATE ../../include)
target_compile_features(digitor_text_project_runtime PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_text_project_runtime PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_text_project_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
