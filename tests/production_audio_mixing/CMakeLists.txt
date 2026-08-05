cmake_minimum_required(VERSION 3.20)
project(DigitorProductionAudioMixing LANGUAGES CXX)

add_executable(digitor_production_audio_mixing
  main.cpp
  ../../src/audio/production_audio_mixing.cpp
)

target_include_directories(digitor_production_audio_mixing PRIVATE ../../include)
target_compile_features(digitor_production_audio_mixing PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_production_audio_mixing PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_production_audio_mixing PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
