cmake_minimum_required(VERSION 3.21)
project(DigitorProductionTimelineContract LANGUAGES CXX)

enable_testing()

add_library(digitor_production_timeline STATIC
    ../../src/editor/production_timeline.cpp)
target_include_directories(digitor_production_timeline PUBLIC ../../include)
target_compile_features(digitor_production_timeline PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(digitor_production_timeline PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(digitor_production_timeline PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(digitor_production_timeline_tests
    ../../tests/production_timeline_tests.cpp)
target_link_libraries(digitor_production_timeline_tests PRIVATE digitor_production_timeline)
target_compile_features(digitor_production_timeline_tests PRIVATE cxx_std_20)

add_test(NAME digitor_production_timeline_tests
    COMMAND digitor_production_timeline_tests)
set_tests_properties(digitor_production_timeline_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;contract")
