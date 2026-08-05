cmake_minimum_required(VERSION 3.20)
project(DigitorGpuExecutionProof LANGUAGES CXX)

add_executable(digitor_gpu_execution_proof
  main.cpp
  ../../src/gpu/gpu_execution_proof.cpp
  ../../src/gpu/gpu_execution_proof_c_api.cpp
)

target_include_directories(digitor_gpu_execution_proof PRIVATE ../../include)
target_compile_features(digitor_gpu_execution_proof PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(digitor_gpu_execution_proof PRIVATE /W4 /WX)
else()
  target_compile_options(digitor_gpu_execution_proof PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
