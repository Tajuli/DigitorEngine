cmake_minimum_required(VERSION 3.21)
project(digitor_release_qualification LANGUAGES C CXX)

set(DIGITOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DIGITOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DIGITOR_ENABLE_FFMPEG OFF CACHE BOOL "" FORCE)
set(DIGITOR_ENABLE_OCIO OFF CACHE BOOL "" FORCE)

add_subdirectory(../.. digitor-engine-build)

add_executable(digitor_release_qualification main.cpp)
target_compile_features(digitor_release_qualification PRIVATE cxx_std_20)
target_link_libraries(digitor_release_qualification PRIVATE Digitor::Engine)

if(MSVC)
  target_compile_options(digitor_release_qualification PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_release_qualification PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME digitor_release_qualification COMMAND digitor_release_qualification)
