# Native GPU validation — RGB Curves v4.7.0

The dedicated `native-rgb-curves-qualification.yml` workflow builds and runs
the hardware-labelled executable on Windows and macOS and uploads its raw log.
Exit code 77 means **skipped because no usable backend was available**, never a
passing execution. Android CI is explicitly compile-only and emits a
machine-readable artifact with `physical_hardware_verified=false`; device and
emulator execution remains a manual/self-hosted qualification responsibility.

The executable records the backend device identity before dispatch and emits
maximum absolute and relative error, RMS, PSNR, SSIM, and worst-pixel data.
The independent CPU reference is evaluated before the measured native interval.
`IRenderBackend::curves_rgba32f` snapshots the atomic reference counter around
the native virtual call, so a successful qualification requires a measured zero
delta and zero fallback calls.

The authoritative audit, call graphs, instrumentation semantics, numerical
gates, and implementation truth table are in
[`grade_rgba32f_execution.md`](grade_rgba32f_execution.md).

Native tests are labeled `hardware` and excluded from ordinary CI. They must
report the actual adapter/device; WARP or another software rasterizer must be
labeled software-adapter validation. Compilation and pipeline creation are not
execution proof. Android requires an NDK build plus emulator/device execution;
iOS requires an Xcode simulator/device run. A missing device is reported as
unavailable, never passed.

Validation builds should enable Vulkan validation layers, D3D12 debug/GPU
validation, Metal API validation, or GLES error checking. Present limitations:
there is no automated debug-message collector, GLES omits an explicit
framebuffer-completeness check, Vulkan uses host-coherent buffers, and no FP16
path exists.

## RGB Curves qualification

No native RGB Curves backend has been hardware-qualified. A future successful run must evidence the curve shader and pipeline identities, FP32 LUT creation/cache hit, four bindings, command recording, dispatch/draw, submission, synchronization, output write/readback, and zero GPU-path CPU-reference and fallback calls. Required FP32 provisional thresholds are max absolute `2e-5`, max relative `2e-5`, with RMS, ULP, PSNR, SSIM, first failure, RGBA values, device and driver reported. FP16 is unsupported; RGBA8 boundary qualification is not claimed.

## Curve provenance contract

Curve runs additionally record compiled and native LUT identities, LUT size/cache disposition, all four bindings, command/dispatch/submission/synchronization/output/readback milestones, identity bypass, failure stage, and separate CPU-curve/fallback counters. Success requires both counters to be zero. Shader compilation, reflection, pipeline, LUT allocation/upload, descriptor, source/destination, recording, submission, synchronization, readback, device-lost and OOM failures must return an explicit error and never claim output.
