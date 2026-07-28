cmake_minimum_required(VERSION 3.21)
project(DigitorConsumer LANGUAGES C CXX)
find_package(DigitorEngine CONFIG REQUIRED)
add_executable(c_consumer main.c)
set_target_properties(c_consumer PROPERTIES C_STANDARD 11)
target_link_libraries(c_consumer PRIVATE Digitor::digitor_engine)
add_executable(cxx_consumer main.cpp)
target_link_libraries(cxx_consumer PRIVATE Digitor::digitor_engine)
enable_testing()
add_test(NAME installed_c_consumer COMMAND c_consumer)
add_test(NAME installed_cxx_consumer COMMAND cxx_consumer)


get_target_property(DIGITOR_IMPORTED_TYPE Digitor::digitor_engine TYPE)
if(WIN32 AND DIGITOR_IMPORTED_TYPE STREQUAL "SHARED_LIBRARY")
  foreach(consumer IN ITEMS c_consumer cxx_consumer)
    add_custom_command(TARGET ${consumer} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${consumer}> $<TARGET_FILE_DIR:${consumer}>
      COMMAND_EXPAND_LISTS)
  endforeach()
endif()
