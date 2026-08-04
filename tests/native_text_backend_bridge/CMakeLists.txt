cmake_minimum_required(VERSION 3.20)
project(digitor_native_text_backend_bridge LANGUAGES CXX)

add_executable(digitor_native_text_backend_bridge
  main.cpp
  ../../src/text/native_text_backend_bridge.cpp
  ../../src/text/native_text_render_runtime.cpp
)

target_include_directories(digitor_native_text_backend_bridge PRIVATE ../../include)
target_compile_features(digitor_native_text_backend_bridge PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(digitor_native_text_backend_bridge PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_native_text_backend_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
