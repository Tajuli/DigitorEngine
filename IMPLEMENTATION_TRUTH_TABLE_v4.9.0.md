# DigitorEngine v4.9.0 Log Wheels Implementation Truth Table

| Requirement | Status | Source evidence | Verification in this environment |
|---|---|---|---|
| FP32 CPU reference | Implemented | `src/gpu/log_wheels.cpp` | Unit tests pass |
| Deterministic parameters/serialization | Implemented | `include/digitor/log_wheels.hpp`, `src/gpu/log_wheels.cpp` | Unit tests pass |
| Native parameter ABI | Implemented | `src/gpu/native_log_wheels.*` | Layout/unit tests pass |
| Render Graph pass | Implemented | `src/gpu/log_wheels.cpp` | Unit tests pass |
| ProcessedGpuFrame contract | Implemented | `src/gpu/gpu_backend.*`, `src/core/engine.*` | Mock-native contract tests pass |
| Silent CPU fallback rejection | Implemented | `IRenderBackend::process_log_wheels_gpu` | Mock-native contract tests pass |
| GPU-source chaining contract | Implemented | `src/gpu/gpu_backend.*` | Mock-native contract tests pass |
| Validation readback separation | Implemented | `src/gpu/gpu_backend.*` | Mock-native contract tests pass |
| Vulkan native dispatch source | Implemented | `src/gpu/vulkan_backend.cpp`, canonical `log_wheels.hlsl` | Host did not build/execute Vulkan target |
| D3D12 native dispatch source | Implemented | `src/gpu/d3d12_backend.cpp`, generated HLSL embedding | Requires Windows build/runtime qualification |
| Metal native dispatch source | Implemented | `src/gpu/metal_backend.mm`, native MSL compute kernel | Requires macOS/iOS build/runtime qualification |
| GLES native dispatch source | Implemented | `src/gpu/gles_backend.cpp`, native GLSL ES fragment path | Requires Android EGL/GLES build/runtime qualification |
| GPU-source chaining backend paths | Implemented in all four source backends | Backend overloads accepting `GpuSourceResource` | Platform runtime qualification pending |
| Validation readback backend paths | Implemented in all four source backends | Backend `execute_validation_readback_log_wheels` overrides | Platform runtime qualification pending |
| Pipeline-cache operation separation | Implemented in D3D12/Metal/GLES; Vulkan uses shader cache key plus operation identity prefix | Backend pipeline helpers | Platform qualification pending |
| Direct GPU preview compatibility | Implemented through `ProcessedGpuFrame` native owners and existing preview consumer path | Four native backend output owners | SharedRenderer Log Wheels UI/policy wiring remains separate work |
| Backend failure injection | Log Wheels paths use existing native failure points at allocation, binding, dispatch, submission, synchronization, frame creation and readback boundaries | Four backend implementations | Full per-backend matrix execution pending |
| Shared Linux build/tests | Passed | CMake + `ctest` | `digitor_tests` and C header pass |
| Real hardware qualification | Not performed | Native GPU test skipped on this host | Not qualified |

“Implemented source” does not mean hardware-qualified. No native backend is marked production-qualified until its platform build, failure matrix and numerical readback suite run on real hardware.
