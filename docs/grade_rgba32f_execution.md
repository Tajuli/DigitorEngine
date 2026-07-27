# `grade_rgba32f` execution audit — v4.6.1

This is a source audit, not a multi-platform hardware report. A checked box is
set only by `ExecutionProvenance` after the corresponding native call. Native
addresses are never exported. No public C ABI changed.

## Entry, validation, selection, and errors

`grade_image_gpu` (`src/gpu/color.cpp`, symbol `grade_image_gpu`) records a
callback which calls `Engine::grade_rgba32f`; the callback itself does no color
math. `Engine::grade_rgba32f` (`src/core/engine.cpp`) locks the engine, checks
initialization, and calls the already-selected `IRenderBackend`. Selection is
`Engine::initialize` -> `create_gpu_backend` -> `select_gpu_backend` ->
`create_native_backend` (`src/core/engine.cpp`, `src/gpu/gpu_backend.cpp`). CPU
fallback occurs only during initialization when the requested GPU cannot be
created and `allow_cpu_fallback` is true. There is no post-selection fallback.
Every backend first verifies equal source/destination element counts; RAII or
explicit cleanup occurs before its error result is returned.

## CPU call graph

`Engine::grade_rgba32f` -> `CpuBackend::grade_rgba32f`
(`src/cpu/cpu_backend.cpp`) -> `grade_image_cpu` -> `grade_color`
(`src/gpu/color.cpp`). `grade_color` executes temperature/tint, luminance,
vibrance/saturation, contrast, lift/gain/offset, exposure, signed gamma, hue
matrix, and alpha copy, in that order. There is no native API.

## Vulkan FP32 call graph

`VulkanBackend::grade_rgba32f` (`src/gpu/vulkan_backend.cpp`) ->
`vkCreateBuffer`/`vkAllocateMemory` for two RGBA32F storage buffers ->
`vkMapMemory` upload -> `vkCreateDescriptorSetLayout` ->
`vkCreatePipelineLayout` -> checked-in `grade_spv` (`grade_spirv.inc`) ->
`vkCreateShaderModule` -> `vkCreateComputePipelines` -> descriptor pool/set and
`vkUpdateDescriptorSets` -> command allocation/begin -> `vkCmdBindPipeline`,
`vkCmdBindDescriptorSets`, `vkCmdPushConstants`, `vkCmdDispatch` ->
`vkQueueSubmit` -> `vkQueueWaitIdle` -> output `vkMapMemory` and byte copy ->
native destruction/free calls. The embedded SPIR-V is non-empty. This path has
real objects and submission, but no qualifying device was available in this
audit, so pixel execution remains hardware-unverified. Host-coherent memory is
required; consequently no non-coherent invalidation branch exists.

## D3D12 FP32 call graph

`D3D12Backend::grade_rgba32f` (`src/gpu/d3d12_backend.cpp`) -> `D3DCompile`
(`cs_5_1`) -> `D3D12SerializeRootSignature` -> `CreateRootSignature` ->
`CreateComputePipelineState` -> `CreateCommittedResource` upload/default/
readback structured buffers -> upload `Map` -> `CreateDescriptorHeap` and
SRV/UAV creation -> command allocator/list reset ->
`SetComputeRootSignature`, descriptor tables/constants, `Dispatch` -> resource
barrier and `CopyResource` -> `ExecuteCommandLists` -> `signal_and_wait`
(fence signal/event wait) -> readback `Map` and copy. COM RAII cleans all local
objects. This structured-buffer path is tightly packed; the separate RGBA8
texture preview path uses `GetCopyableFootprints` and row-pitch-aware copying.
D3DCompile emits DXBC, **not DXIL**; therefore the requested DXIL qualification
is not met and the backend is not called hardware verified.

## Metal FP32 call graph

`MetalBackend::grade_rgba32f` (`src/gpu/metal_backend.mm`) ->
`newLibraryWithSource` -> `newFunctionWithName` ->
`newComputePipelineStateWithFunction` -> command queue and shared input/output
`MTLBuffer` allocation/upload -> command buffer/compute encoder -> buffer/byte
bindings -> `dispatchThreads` -> `endEncoding`, `commit`,
`waitUntilCompleted` -> require `MTLCommandBufferStatusCompleted` -> copy shared
output contents. ARC/autorelease cleanup owns temporary objects. No macOS/iOS
execution was performed here.

## OpenGL ES FP32 call graph

`GlesBackend::grade_rgba32f` (`src/gpu/gles_backend.cpp`) -> vertex/fragment
`glCreateShader`, `glShaderSource`, `glCompileShader` and status checks ->
`glCreateProgram`, attach/link and link check -> RGBA32F source/destination
`glTexStorage2D`, source `glTexSubImage2D` -> framebuffer attachment ->
`glUseProgram`, sampler/uniform binding -> `glDrawArrays` -> `glReadPixels` ->
`glFinish`, `glGetError`, and object deletion. GLES initialization requires an
existing current context. Framebuffer completeness is not currently checked;
this is a known qualification gap. No Android emulator/device ran here.

## Instrumentation and failure contract

`ExecutionProvenance` (`src/gpu/execution_provenance.hpp`) records backend,
CPU/GPU mode, stable device/compiler/shader/pipeline strings, upload, recording,
dispatch/draw, submit, synchronization, output, readback, CPU-reference and
fallback counts, error/device-lost state, and cache disposition. Backend flags
are written only after the native boundary is crossed. `grade_image_cpu`
increments the reference counter; each backend reports the invocation delta for
its call. `GpuFailurePoint` covers compilation, reflection, pipeline,
descriptors, both allocations, upload, recording, submit, synchronization,
readback, device loss, and OOM. Injected failures return an error without
writing output or invoking CPU color work. This internal C++ seam does not
cross the C ABI.

No grade graph/shader/pipeline cache exists in these direct backend functions;
cache fields truthfully report `NotApplicable`. Thus persistent reload,
cross-context reuse, corruption recovery, concurrent deduplication, and stale
parameter-buffer claims are not applicable—not silently treated as passes.

## Color coverage and precision

All accepted `ColorGrade` fields (exposure, contrast, gamma, lift, gain,
offset, temperature, tint, saturation, vibrance, and hue matrix) have CPU and
native FP32 expressions in identical order. Tests use non-default,
non-symmetric parameters, negative/over-range RGB, and fractional alpha. Alpha
is copied. No clipping or tone-map stage is performed. Curves, primary/log
wheels, HSL qualifier, LUT, and arbitrary matrix parameters are unsupported.
All four native grade paths are FP32 only; FP16 is unsupported and never
credited as FP32 evidence.

## Qualification truth table

| Backend | Compiles | Real Shader | Real Native Pipeline | Dispatch/Draw Submitted | Pixel Output Validated | CPU Color Calls During GPU Run | Environment | Status |
|---|---|---|---|---|---|---|---|---|
| Vulkan FP32 | Yes when Vulkan SDK is found | Yes, SPIR-V | Yes | Source/test conditional; not run here | No qualifying run | Required 0 | Linux audit host; no device run | Implemented, hardware-unverified |
| Vulkan FP16 | N/A | No | No | No | No | N/A | All | Unsupported |
| D3D12 FP32 | Windows CI source configuration | Yes, DXBC (not requested DXIL) | Yes | Source/test conditional; not run here | No qualifying run | Required 0 | Windows hardware not run here | Implemented, hardware-unverified |
| D3D12 FP16 | N/A | No | No | No | No | N/A | All | Unsupported |
| Metal FP32 | Apple source configuration | Yes, runtime MSL | Yes | Source/test conditional; not run here | No qualifying run | Required 0 | macOS/iOS hardware not run here | Implemented, hardware-unverified |
| Metal FP16 | N/A | No | No | No | No | N/A | All | Unsupported |
| GLES FP32 | Android source configuration | Yes, GLSL ES | Yes, linked GL program | Source/test conditional; not run here | No qualifying run | Required 0 | Android device/emulator not run here | Implemented, hardware-unverified |
| GLES FP16 | N/A | No | No | No | No | N/A | All | Unsupported |
| CPU FP32 reference | Yes | N/A | N/A | N/A | Unit tests | 1 per image call | Linux host | Production implementation, software-adapter verified |

The Windows-only `test_native_gpu.cpp` is the executable evidence gate: it
creates each available native backend, independently computes the CPU
reference outside the GPU call, checks provenance including zero CPU calls,
and reports maximum absolute error, RMS, PSNR and SSIM. Its current threshold
is max absolute error `<2e-5` and SSIM `>0.99999`. It also validates 1x1, 2x2,
3x2, 7x5, 63x17, 65x3, and 257x2 upload/readback patterns. Results are not
recorded as passed unless that executable actually runs. Vulkan absence is a
skip; D3D12 availability is required by the Windows hardware-labeled test.
