# Implementation status — v4.7.0

## RGB Curves direct-preview qualification truth table

No device execution was available while preparing this source change. A
compiled shader is not an execution result, and validation readback is not
direct preview consumption. Implemented rows therefore remain
hardware-unverified until a qualification artifact records execution.

| Backend | Native Curve Shader | Native Texture Output | Direct Preview Consumption | Execution Test | Numerical Metrics | CPU Curve Delta | Environment | Status |
|---|---|---|---|---|---|---|---|---|
| Vulkan FP32 | `VulkanBackend::execute_process_curves_gpu` | RGBA32F `VkImage` | GPU image copy | backend-capable, hardware labeled | emitted only when executed | measured around dispatch | not executed here | Implemented, hardware-unverified |
| D3D12 FP32 | `D3DBackend::execute_process_curves_gpu` | RGBA32F texture UAV | GPU texture copy | backend-capable, hardware labeled | emitted only when executed | measured around dispatch | not executed here | Implemented, hardware-unverified |
| Metal FP32 | `MetalBackend::execute_process_curves_gpu` | RGBA32F `MTLTexture` | GPU texture blit | macOS entry point | emitted only when executed | measured around dispatch | not executed here | Implemented, hardware-unverified |
| GLES FP32 | `GlBackend::execute_process_curves_gpu` | retained RGBA32F texture | direct framebuffer sampling | Android entry point | emitted only when executed | measured around draw | not executed here | Implemented, hardware-unverified |
| CPU FP32 reference | `CompiledRgbCurves::apply` | Not applicable | No | `test_rgb_curves` | deterministic reference assertions | atomic counter increments | Ubuntu CPU | Production implementation, software-adapter verified |

| Backend | Native GPU Preview | Direct GPU Preview Consumption | Normal Preview CPU Readback | Validation Readback |
|---|---|---|---|---|
| Vulkan FP32 | retained RGBA32F `VkImage` | GPU copy into preview image | prohibited | Available |
| D3D12 FP32 | retained RGBA32F texture UAV | GPU copy into preview texture | prohibited | Available |
| Metal FP32 | RGBA32F `MTLTexture` | GPU-only preview-texture blit | prohibited | Available |
| GLES FP32 | retained RGBA32F texture | samples retained texture | prohibited | Available |
| CPU FP32 reference | Not applicable | No | Not applicable | Direct CPU result |

| Backend | Native Texture Output | ProcessedGpuFrame Override | Direct Preview Consumption | Normal Preview Readback | Validation Readback | Evidence | Status |
|---|---|---|---|---|---|---|---|
| Vulkan FP32 | RGBA32F `VkImage` | yes | GPU-to-GPU preview image copy | zero by contract | separate buffer readback | `digitor_native_gpu_tests` | Implemented, hardware-unverified |
| D3D12 FP32 | RGBA32F texture UAV | yes | GPU-to-GPU preview texture copy | zero by contract | separate readback resource | `digitor_native_gpu_tests` | Implemented, hardware-unverified |
| Metal FP32 | RGBA32F `MTLTexture` | yes | GPU-only preview texture blit | zero by contract | separate buffer validation | `digitor_native_gpu_tests` | Implemented, hardware-unverified |
| GLES FP32 | retained RGBA32F texture | yes | sampled into current framebuffer | zero by contract | separate `glReadPixels` validation | `digitor_native_gpu_tests` | Implemented, hardware-unverified |
| CPU FP32 reference | No | not applicable | CPU frame | not applicable | direct | `test_rgb_curves` | Production implementation, software-adapter verified |

The CPU equations in `src/gpu/color.cpp` remain authoritative. The canonical
compute source is `src/gpu/shaders/color_pipeline.hlsl`; native backend adapters
preserve its operation order. “Implemented, hardware-unverified” means real
native commands exist but this Linux build supplied no qualifying hardware run.
It must never be interpreted as “Passed”.

| Feature | CPU Reference | Vulkan | D3D12 | Metal | GLES | Evidence | Status |
|---|---|---|---|---|---|---|---|
| Exposure | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Contrast | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Saturation | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Temperature | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Tint | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Lift | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Gamma | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Gain | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Offset | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| HSV (hue rotation) | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |
| Matrix (hue rotation) | yes | implemented | implemented | implemented | implemented | canonical shader; CPU comparison | Implemented, hardware-unverified |
| ToneMap | yes (passthrough) | no stage | no stage | no stage | no stage | color-science passthrough test | CPU reference only |
| Vibrance | yes | implemented | implemented | implemented | implemented | canonical shader; native validation harness | Implemented, hardware-unverified |

RGB curves, primary/log wheels, HSL qualifier, and LUT execution are **not
implemented** by this milestone. The graph rejects non-identity future-stage
parameters rather than skipping them or falling back to CPU. Preview/export
equivalence is not claimed. The validation report contains maximum absolute and
relative error, RMS, PSNR, SSIM, first failing pixel, worst pixel, backend, and
precision. Native hardware remains unverified on all four backends for this
commit.

## v4.6.1 native grade qualification

The source-level audit and exact qualification truth table are maintained in
[`grade_rgba32f_execution.md`](grade_rgba32f_execution.md). This host audit did
not execute qualifying Vulkan, D3D12, Metal, GLES, Android, or iOS hardware.
Native compilation is not reported as pixel validation; FP16 is unsupported.
The internal provenance/failure seams prove that failures do not silently run
the CPU reference in non-hardware tests.

## v4.7 native execution detail

Environment: Ubuntu/GCC 13 CPU-only container; no GPU/device/driver execution. The canonical shader source is not hardware evidence. Numerical GPU results are therefore `not measured`, honestly, and the executed metrics remain unavailable on this host.

Metal and OpenGL ES now contain real native curve dispatch paths (runtime shader
compilation, FP32 LUT binding, ordered Master/R/G/B evaluation, synchronization,
and retained texture output). They remain hardware-unverified in this Linux environment. Vulkan and D3D12 also contain native texture compute dispatch and GPU-to-GPU preview routing.
All four paths remain hardware-unverified pending device qualification.

| Feature | CPU Reference | Vulkan | D3D12 | Metal | GLES | Evidence | Numerical Result | Status |
|---|---|---|---|---|---|---|---|---|
| Native FP32 LUT resource | separate CPU LUT | storage buffer | upload buffer SRV | MTLBuffer | R32F texture | native backend source | not hardware measured | Implemented, hardware-unverified |
| 256/1024/4096 LUT execution | yes | compute dispatch | compute dispatch | compute dispatch | framebuffer draw | native dispatch source; CPU deterministic fixtures | GPU not measured on this host | Implemented, hardware-unverified |
| Master + Red + Green + Blue | fixed order | fixed order | fixed order | fixed order | fixed order | canonical HLSL and translated native shaders | GPU not measured on this host | Implemented, hardware-unverified |
| Negative/over-range values | yes | parameter-buffer extrapolation | root-constant extrapolation | constant-buffer extrapolation | uniform extrapolation | native shader source | GPU not measured on this host | Implemented, hardware-unverified |
| Alpha preservation | exact | preserved | preserved | preserved | preserved | native shader source | GPU not measured on this host | Implemented, hardware-unverified |
| Render Graph integration | CPU pass | backend-neutral node | backend-neutral node | backend-neutral node | backend-neutral node | graph pass source | non-numerical | Implemented, hardware-unverified |
| Persistent shader artifact cache | n/a | SPIR-V by compiler/device key | DXIL by compiler/device key | driver cache | driver cache | `ShaderCompiler` persistent cache | non-numerical | Implemented, hardware-unverified |
| Native LUT cache | n/a | device-key model | device-key model | device-key model | device-key model | cold/warm/device-key unit test | non-numerical | Implemented, hardware-unverified |
| Preview consumption | CPU backend only | native curve output | native curve output | native curve output | native curve output | `SharedRenderer::set_rgb_curves` routing | not hardware measured | Implemented, hardware-unverified |
| Provenance | CPU fields | dispatch/bind/cache/readback | dispatch/bind/cache/readback | dispatch/bind/cache/readback | draw/bind/cache/readback | backend provenance assignments | CPU/fallback count zero by path | Implemented, hardware-unverified |
| Failure injection | CPU validation | explicit error | explicit error | explicit error | explicit error | failure seam before native work | no CPU fallback | Implemented, hardware-unverified |
| FP32 | yes | RGBA32F image | RGBA32F texture | RGBA32F texture | RGBA32F texture | native resource formats | not hardware measured | Implemented, hardware-unverified |
| FP16 | Unsupported | Unsupported | Unsupported | Unsupported | Unsupported | no implementation | none | Unsupported |
