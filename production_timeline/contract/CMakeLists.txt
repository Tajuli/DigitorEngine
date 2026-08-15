cmake_minimum_required(VERSION 3.21)
project(DigitorProductionTimelineContract LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)

add_library(digitor_production_timeline STATIC
    ../../src/editor/production_timeline.cpp
    ../../src/editor/production_timeline_runtime.cpp
    ../../src/editor/production_timeline_playback.cpp
    ../../src/editor/production_timeline_automation.cpp
    ../../src/editor/timeline.cpp
    ../../src/audio/audio_mixer.cpp
    ../../src/audio/audio_time_transform.cpp
    ../../src/audio/audio_dynamics.cpp
    ../../src/audio/professional_audio_engine.cpp
    ../../src/audio/production_audio_media_pipeline.cpp
    ../../src/sdk/timeline_audio_session.cpp
    production_audio_media_contract_stubs.cpp)
target_include_directories(digitor_production_timeline PUBLIC ../../include)
target_compile_features(digitor_production_timeline PUBLIC cxx_std_20)
target_compile_definitions(digitor_production_timeline PUBLIC DIGITOR_ENGINE_STATIC)
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
    production_timeline_automation_tests
    audio_mixer_tests
    audio_time_transform_tests
    audio_dynamics_tests)
    add_executable(digitor_${test_name} ../../tests/${test_name}.cpp)
    target_link_libraries(digitor_${test_name} PRIVATE digitor_production_timeline)
    target_compile_features(digitor_${test_name} PRIVATE cxx_std_20)
    add_test(NAME digitor_${test_name} COMMAND digitor_${test_name})
endforeach()

add_executable(digitor_timeline_audio_session_tests
    ../../tests/timeline_audio_session_c_api_tests.cpp)
target_link_libraries(digitor_timeline_audio_session_tests PRIVATE digitor_production_timeline)
target_compile_features(digitor_timeline_audio_session_tests PRIVATE cxx_std_20)
add_test(NAME digitor_timeline_audio_session_tests COMMAND digitor_timeline_audio_session_tests)

set_tests_properties(digitor_production_timeline_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;contract")
set_tests_properties(digitor_production_timeline_runtime_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;runtime;integration")
set_tests_properties(digitor_production_timeline_playback_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;playback;seek;revision;parity")
set_tests_properties(digitor_production_timeline_automation_tests PROPERTIES
    LABELS "production-timeline;v5.1.0;keyframe;transition;parity")
set_tests_properties(digitor_audio_mixer_tests PROPERTIES
    LABELS "production-audio;v5.2.0;mixer;dsp;parity")
set_tests_properties(digitor_audio_time_transform_tests PROPERTIES
    LABELS "production-audio;v5.3.0;resampler;time-stretch;pitch;parity")
set_tests_properties(digitor_audio_dynamics_tests PROPERTIES
    LABELS "production-audio;v5.4.0;eq;compressor;limiter;loudness;parity")
set_tests_properties(digitor_timeline_audio_session_tests PROPERTIES
    LABELS "production-integration;v5.5.0;c-abi;flutter;timeline;audio")
