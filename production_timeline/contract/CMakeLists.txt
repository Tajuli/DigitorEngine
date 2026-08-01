cmake_minimum_required(VERSION 3.21)
project(DigitorProductionTimelineContract LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)

add_library(digitor_production_timeline STATIC
    ../../src/editor/production_timeline.cpp
    ../../src/editor/production_timeline_runtime.cpp
    ../../src/editor/production_timeline_playback.cpp
    ../../src/editor/production_timeline_automation.cpp
    ../../src/editor/timeline.cpp)
target_include_directories(digitor_production_timeline PUBLIC ../../include)
target_compile_features(digitor_production_timeline PUBLIC cxx_std_20)
target_link_libraries(digitor_production_timeline PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(digitor_production_timeline PRIVATE /W4 /permissive- /WX)
else()
    target_compile_options(digitor_production_timeline PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

foreach(test_name IN ITEMS
    production_timeline_tests
    production_timeline_runtime_tests
    production_timeline_playback_tests
    production_timeline_automation_tests)
    add_executable(digitor_${test_name} ../../tests/${test_name}.cpp)
    target_link_libraries(digitor_${test_name} PRIVATE digitor_production_timeline)
    target_compile_features(digitor_${test_name} PRIVATE cxx_std_20)
    add_test(NAME digitor_${test_name} COMMAND digitor_${test_name})
endforeach()

set_tests_properties(digitor_production_timeline_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;contract")
set_tests_properties(digitor_production_timeline_runtime_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;runtime;integration")
set_tests_properties(digitor_production_timeline_playback_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;playback;seek;revision;parity")
set_tests_properties(digitor_production_timeline_automation_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;keyframe;transition;parity")
